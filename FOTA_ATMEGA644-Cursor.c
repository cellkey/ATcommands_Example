/*==============================================================================
  BootLoader.c  —  FOTA Reception, Conversion & Validation
  Project  : EazyGate_1_2_18 / EazyGate_1_2_19
  Target   : ATmega644P  @  7.3728 MHz        (CodeVisionAVR)
  Modem    : SIMCom A7670E — buffered TCP/IP  (AT+CIPRXGET)
  EEPROM   : Two 32 KB blocks, I2C            (twiWriteExtMemN / twiReadEEP1Byte)

  ── RECEIVED FILE FORMAT ────────────────────────────────────────────────────
  After the modem AT-command response header, the raw file body is:

      <file_size_decimal>\r\n          e.g.  "98432\r\n"
      <XXXX>[T]<first_intel_hex_row>\r\n   e.g.  "<3D85>[1]:0A000000..7C\r\n"
      :<LL><AAAA><TT><DD..><CC>\r\n    subsequent Intel HEX rows
      …
      :00000001FF\r\n                  Intel HEX EOF record

     <XXXX>  4-digit hex  =  Σ (checksum byte of every DATA record, type 0x00)
             EOF record (type 0x01) is NOT included in the sum.
     [T]     1 = flash firmware,  2 = EEPROM data

  ── EEPROM STORAGE FORMAT ───────────────────────────────────────────────────
  The existing bootloader reads binary Intel HEX records, so storage is:

      Byte 0 : '[' (0x5B)
      Byte 1 : file_type  (0x01 = flash)
      Byte 2 : ']' (0x5D)
      Byte 3+: For every Intel HEX data record (type 0x00):
                 0x3A  byte_count  addr_H  addr_L  0x00  [data_bytes]  checksum
      Last 6 : 0x3A  0x00  0x00  0x00  0x01  0xFF   (EOF record)

  This binary format is byte-for-byte identical to what the original Ascii2Bin()
  produced, so the existing bootloader continues to work without changes.

  ── PUBLIC API ──────────────────────────────────────────────────────────────
      unsigned int RxUpdateFile(void)
          Receives the firmware file from the server, converts each ASCII
          Intel HEX row to its binary equivalent, and stores the result in
          the external EEPROM page by page (128 bytes each).
          Returns: total bytes written (>0 = success), or 0 on any failure.

      int ValidityCheck(unsigned int start_addr)
          Re-reads the stored binary data from EEPROM, re-verifies every
          row's two's-complement checksum, and compares the accumulated
          vertical checksum against the target captured during reception.
          Returns: 0 = VALID,  99 = INVALID / ERROR.

  ── KEY IMPROVEMENTS OVER ORIGINAL ─────────────────────────────────────────
  1. No Timer2 interrupt for FOTA (it conflicted with BLE UART1 reception).
     Rows are parsed directly in the main loop — fast enough at 7.37 MHz.
  2. Row scanner state persists across chunk boundaries, eliminating the
     need for a separate carry buffer or ping-pong buffers.
  3. Correct Intel HEX checksum: sum of all record bytes (incl. CS) == 0.
  4. All record types are handled (type 0=data, type 1=EOF, others skipped).
  5. Vertical checksum correctly excludes the EOF record.
  6. EEPROM write retries on I2C failure with clear error reporting.
  7. Block-1 cross-over handled during both write and read phases.
  8. `vert_cs_target` is module-level, shared between RxUpdateFile and
     ValidityCheck without any global side-effects.
  9. No `RowsCount` dependency in ValidityCheck — termination is purely
     by EOF record detection.
==============================================================================*/

#include <string.h>
#include "define.h"
#include "twi.h"

/*──────────────────────────────────────────────────────────────────────────────
  CONFIGURATION
──────────────────────────────────────────────────────────────────────────────*/

/* EEPROM geometry */
#define PAGE_SIZE        128U       /* bytes per I2C page-write operation         */
#define BLOCK_BYTES      65536UL    /* bytes per I2C block (block0=0xA0, block1=0xA2) */

/* Modem TCP read */
#define CHUNK_MAX        512U       /* bytes requested per AT+CIPRXGET=2 command  */

/* Row buffer: max Intel HEX row = ':' + 2+4+2+32+2 chars + CRLF + '\0' = 45+  */
#define ROW_BUF_SIZE     52U

/* EEPROM write reliability */
#define WRITE_TRIES      3          /* I2C write attempts before permanent failure */

/*
  heat_time ticks — Timer1 decrements heat_time approximately every 100 ms.
  These timeout values are chosen conservatively for a 7.3728 MHz CPU.
*/
#define T_INIT           30         /* 3.0 s — wait for first data from server    */
#define T_QUERY          12         /* 1.2 s — per AT+CIPRXGET=4 attempt          */
#define T_CHUNK          25         /* 2.5 s — per AT+CIPRXGET=2 read             */

/*──────────────────────────────────────────────────────────────────────────────
  HELPER MACROS  —  EEPROM block/address from a linear long position
──────────────────────────────────────────────────────────────────────────────*/
#define EEP_BLOCK(pos)   ((char)(((pos) >= BLOCK_BYTES) ? 1 : 0))
#define EEP_ADDR(pos)    ((unsigned int)((pos) & 0xFFFFUL))

/*──────────────────────────────────────────────────────────────────────────────
  MODULE-LEVEL STATE  (private — one FOTA session at a time)
──────────────────────────────────────────────────────────────────────────────*/

/* ── Binary page accumulator ─────────────────────────────────────────────── */
static char          pg_buf[PAGE_SIZE]; /* bytes waiting to be written to EEPROM */
static unsigned char pg_fill;           /* how many bytes are in pg_buf (0..128) */
static long          pg_pos;            /* absolute EEPROM byte address of page   */

/* ── Intel HEX row scanner  (persists across chunk boundaries) ───────────── */
static char          row_buf[ROW_BUF_SIZE]; /* chars collected for the current row */
static unsigned char row_fill;          /* chars in row_buf                       */
static char          in_row;            /* TRUE = inside a row (after ':')        */

/* ── Checksum state ──────────────────────────────────────────────────────── */
static unsigned int  vert_cs_target;    /* from <XXXX> in file header             */
static unsigned int  vert_cs_calc;      /* Σ row-checksum bytes of data records   */

/* ── Progress & status ───────────────────────────────────────────────────── */
static unsigned int  rows_stored;       /* data rows successfully processed       */
static unsigned char fota_file_type;    /* 1=flash, 2=EEPROM                      */
static char          eof_stored;        /* TRUE after the EOF record is stored    */

/* ── Sequential EEPROM reader (for ValidityCheck) ───────────────────────── */
static long          rd_pos;            /* current absolute read position         */
static unsigned char rd_seed;           /* first byte from twiReadEEP1Byte seed   */
static char          rd_first;          /* TRUE = return rd_seed on next rd_byte()*/

/*──────────────────────────────────────────────────────────────────────────────
  EXTERN REFERENCES  (symbols provided by other translation units)
──────────────────────────────────────────────────────────────────────────────*/

extern char     RxUart0Buf[];           /* 720-byte modem receive buffer          */
extern char     ComBuf[];               /* 64-byte TX scratch buffer              */
extern char     chksumBuf[];            /* 4-byte checksum temp buffer            */

extern volatile unsigned int rx0_buff_len;
extern unsigned int rx_counter0, rx_counter1;
extern int      heat_time;
extern int      ServerResponseTimeOut;
extern char     cntr;
extern BYTE     mainTask;

extern bit  Modem_Message_Recieved;
extern bit  FirmwareUpdateTime;
extern bit  UpdateSession;
extern bit  bWaitForModemAnswer;
extern bit  ServerComOn;
extern bit  FOTA_process;

extern int   twiWriteExtMemN(char block, unsigned int addr,
                             unsigned char n, char *pdata);
extern int   twiWriteExtMem1Byte(char block, unsigned int addr, char data);
extern char  twiReadEEP1Byte(char block, unsigned int InternalAddr);
extern char  ReadEEPCurrentAddress(void);

extern void  UART0_WriteMsg(char *InStr);
extern void  UART1_WriteMsg(char *InStr);
extern void  _putchar1(char c);
extern void  putchar1(char c);

extern unsigned char ChkNodemRespons(char flash *str1,
                                     char flash *str2,
                                     unsigned char timeout);
extern void  CIPSEND_Lengh(char count);
extern void  SendDebugMsg(flash char *bufToSend);
extern void  PrintNum(long val);
extern void  SendPostInfo(char index);


/*==============================================================================
  SECTION 1 — HEX DIGIT HELPERS
==============================================================================*/

/*
  hex_nibble — Convert one ASCII hex character to its 4-bit numeric value.
  Accepts '0'-'9', 'A'-'F', 'a'-'f'.
*/
static unsigned char hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return (unsigned char)(c - '0');
    return (unsigned char)((c & 0xDF) - 'A' + 10);  /* strips bit5, handles a-f too */
}

/*
  hex_byte — Parse two ASCII hex characters into one byte; advance *pp by 2.
  Example: hex_byte(&p) on "3A..." returns 0x3A and advances p by 2.
*/
static unsigned char hex_byte(char **pp)
{
    unsigned char b = (hex_nibble((*pp)[0]) << 4) | hex_nibble((*pp)[1]);
    *pp += 2;
    return b;
}

/*
  hex_word — Parse four ASCII hex characters into a 16-bit word (big-endian);
  advance *pp by 4.
*/
static unsigned int hex_word(char **pp)
{
    unsigned int w = ((unsigned int)hex_byte(pp) << 8) | hex_byte(pp);
    return w;
}


/*==============================================================================
  SECTION 2 — EEPROM PAGE WRITE
==============================================================================*/

/*
  eeprom_write_page — Write 'len' bytes from 'src' to EEPROM at absolute
  position 'write_pos' (handles block selection automatically).
  Retries up to WRITE_TRIES times on I2C NACK before returning failure.
  Returns 1 on success, 0 on permanent failure.
*/
static char eeprom_write_page(long write_pos, char *src, unsigned char len)
{
    char          blk  = EEP_BLOCK(write_pos);
    unsigned int  addr = EEP_ADDR(write_pos);
    unsigned char attempts;

    for (attempts = 0; attempts < WRITE_TRIES; attempts++)
    {
        #asm("wdr");                            /* reset watchdog during long writes */
        if (twiWriteExtMemN(blk, addr, len, src))
        {
            delay_ms(5);                        /* tWR: EEPROM internal write cycle  */
            return 1;
        }
        delay_ms(10);                           /* brief back-off before retry        */
    }

    sprintf(ComBuf, "\r\nEEPROM write ERR @ 0x%04X (blk %d)\r\n\0",
            addr, (int)blk);
            UART1_WriteMsg(ComBuf);
    return 0;
}

/*
  flush_page — Write the current pg_buf contents to EEPROM at pg_pos, then
  advance pg_pos and reset pg_fill to 0.
  No-op if the buffer is empty.  Returns 1 on success, 0 on failure.
*/
static char flush_page(void)
{
    char ok;

    if (pg_fill == 0)
        return 1;

    ok = eeprom_write_page(pg_pos, pg_buf, pg_fill);
    if (ok)
    {
        pg_pos  += pg_fill;
        pg_fill  = 0;
    }
    return ok;
}

/*
  store_byte — Append one binary byte to pg_buf.
  When the buffer is full (128 bytes), it is automatically flushed to EEPROM.
  Returns 1 on success, 0 on EEPROM write failure.
*/
static char store_byte(unsigned char b)
{
    pg_buf[pg_fill++] = (char)b;
    if (pg_fill >= PAGE_SIZE)
        return flush_page();
    return 1;
}


/*==============================================================================
  SECTION 3 — INTEL HEX ROW PROCESSOR
==============================================================================*/

/*
  store_hex_row — Parse the complete ASCII Intel HEX row in row_buf[], convert
  each field to binary, verify the row checksum, and store the binary
  representation in EEPROM.

  EEPROM binary format per record:
      0x3A  byte_count  addr_H  addr_L  rec_type  [data_bytes]  checksum

  Intel HEX checksum rule:
      (byte_count + addr_H + addr_L + rec_type + Σdata + checksum) mod 256 == 0

  Vertical checksum:
      vert_cs_calc += checksum   for DATA records (type 0x00) only.
      EOF record (type 0x01) is stored but NOT added to vert_cs_calc.

  Return values:
      1 = data record stored OK
      2 = EOF record stored  (reception should stop)
      0 = error  (bad format or checksum mismatch)
*/
static char store_hex_row(void)
{
    char         *p;
    unsigned char byte_count, rec_type, row_cs, db;
    unsigned int  load_addr;
    unsigned char k, sum;

    /* ── Basic sanity check ── */
    if (row_buf[0] != ':')
    {
        SendDebugMsg("Bad row: missing ':'\r\n\0");
        return 0;
    }

    p = row_buf + 1;                        /* skip ':' */

    /* ── Parse the four fixed header fields ── */
    byte_count = hex_byte(&p);              /* LL   — data byte count             */
    load_addr  = hex_word(&p);              /* AAAA — 16-bit load address          */
    rec_type   = hex_byte(&p);             /* TT   — record type                  */

    /* Running sum for checksum verification (accumulate as unsigned char) */
    sum = byte_count
        + (unsigned char)(load_addr >> 8)
        + (unsigned char)(load_addr & 0xFF)
        + rec_type;

    /* ──────────────────────────────────────────────────────────────────────
       EOF record  (type 0x01)
       Store the 6-byte binary EOF record and signal end-of-file.
    ────────────────────────────────────────────────────────────────────── */
    if (rec_type == 0x01)
    {
        row_cs = hex_byte(&p);
        sum   += row_cs;

        if (sum != 0x00)
        {
            SendDebugMsg("EOF row checksum ERR!\r\n\0");
            return 0;
        }

        /* Store binary EOF record exactly as original Ascii2Bin did */
        if (!store_byte(0x3A)) return 0;    /* ':'        */
        if (!store_byte(0x00)) return 0;    /* count = 0  */
        if (!store_byte(0x00)) return 0;    /* addr H     */
        if (!store_byte(0x00)) return 0;    /* addr L     */
        if (!store_byte(0x01)) return 0;    /* type = EOF */
        if (!store_byte(0xFF)) return 0;    /* checksum   */

        return 2;                           /* tell caller: done  */
    }

    /* ──────────────────────────────────────────────────────────────────────
       Extended address records  (type 0x02, 0x04, etc.)
       ATmega644 is a plain 16-bit address device; skip these records.
    ────────────────────────────────────────────────────────────────────── */
    if (rec_type != 0x00)
    {
        sprintf(ComBuf, "Skipping rec type=0x%02X\r\n\0", (unsigned int)rec_type);
        UART1_WriteMsg(ComBuf);
        return 1;                           /* not an error, just ignored */
    }

    /* ──────────────────────────────────────────────────────────────────────
       Data record  (type 0x00)
       Store binary header: ':', byte_count, addr_H, addr_L, 0x00
    ────────────────────────────────────────────────────────────────────── */
    if (!store_byte(0x3A))                              return 0; /* ':'     */
    if (!store_byte(byte_count))                        return 0; /* LL      */
    if (!store_byte((unsigned char)(load_addr >> 8)))   return 0; /* addr H  */
    if (!store_byte((unsigned char)(load_addr & 0xFF))) return 0; /* addr L  */
    if (!store_byte(rec_type))                          return 0; /* type 00 */

    /* ── Store data bytes ── */
    for (k = 0; k < byte_count; k++)
    {
        db   = hex_byte(&p);
        sum += db;
        if (!store_byte(db)) return 0;
    }

    /* ── Verify and store the row checksum byte ── */
    row_cs  = hex_byte(&p);
    sum    += row_cs;

    /*
      Intel HEX checksum rule: the two's complement of the sum of all
      preceding bytes.  Equivalently, the sum of ALL bytes including the
      checksum byte must equal 0x00 (mod 256).
    */
    if (sum != 0x00)
    {
        sprintf(ComBuf, "\r\nRow %u CS ERR: sum=0x%02X\r\n\0",
                rows_stored + 1, (unsigned int)sum);
        UART1_WriteMsg(ComBuf);
        return 0;
    }

    if (!store_byte(row_cs)) return 0;

    /* ── Accumulate vertical checksum (data records only) ── */
    vert_cs_calc += (unsigned int)row_cs;
    rows_stored++;

    /* Progress indicator: one dot every 64 rows (~1 KB of binary data) */
    if ((rows_stored & 0x3F) == 0)
        _putchar1('.');

    return 1;
}


/*==============================================================================
  SECTION 4 — CHUNK SCANNER
==============================================================================*/

/*
  scan_chunk — Scan 'len' bytes of raw ASCII hex payload for complete Intel
  HEX rows.  The scanner state (in_row, row_buf, row_fill) persists between
  calls, so rows that span chunk boundaries are handled transparently.

  A row starts when ':' is encountered outside a row (in_row == 0).
  A row ends at the first '\n' character.  '\r' is silently ignored.
  Any character between chunks before the next ':' is discarded.

  Returns 1 if all rows in this chunk were processed OK, 0 on any error.
*/
static char scan_chunk(char *data, unsigned int len)
{
    unsigned int i;
    char         c, res;

    for (i = 0; i < len; i++)
    {
        c = data[i];

        if (!in_row)
        {
            /*
              Looking for the start of a new row.  Only ':' triggers entry.
              All other characters (whitespace, CR/LF, leftover modem bytes)
              are safely ignored.
            */
            if (c == ':')
            {
                row_fill        = 0;
                row_buf[row_fill++] = c;
                in_row          = 1;
            }
        }
        else
        {
            /* Inside a row — collect characters until '\n' */

            if (c == '\r')
                continue;               /* CR before LF — skip silently           */

            if (c == '\n')
            {
                /* Row complete — null-terminate and process */
                row_buf[row_fill] = '\0';
                in_row            = 0;
                row_fill          = 0;

                res = store_hex_row();
                if (res == 0) return 0;         /* checksum or format error        */
                if (res == 2)                   /* EOF record stored               */
                {
                    eof_stored = 1;
                    return 1;
                }
            }
            else
            {
                /* Accumulate row character */
                if (row_fill < (ROW_BUF_SIZE - 1))
                {
                    row_buf[row_fill++] = c;
                }
                else
                {
                    /* Row buffer overflow — data is corrupt */
                    SendDebugMsg("Row buf overflow! Abort.\r\n\0");
                    in_row   = 0;
                    row_fill = 0;
                    return 0;
                }
            }
        }
    }

    return 1;   /* chunk processed without error (row may be partially built) */
}


/*==============================================================================
  SECTION 5 — MODEM COMMUNICATION
==============================================================================*/

/*
  modem_set_buffered — Configure A7670E for manual (on-demand) receive mode.
  In this mode the modem buffers incoming TCP data; we pull it with
  AT+CIPRXGET=2 when ready.
*/
static void modem_set_buffered(void)
{
    sprintf(ComBuf, "AT+CIPRXGET=1\r\n\0");
    UART0_WriteMsg(ComBuf);
    delay_ms(50);
}

/*
  modem_query_available — Query how many bytes are waiting in the A7670E
  TCP receive buffer for link 1.
  Command:  AT+CIPRXGET=4,1
  Response: +CIPRXGET: 4,1,<count>\r\nOK

  Retries up to 3 times on timeout or parse failure.
  Returns byte count, or 0 if no data or no response.
*/
static unsigned int modem_query_available(void)
{
    char         *ptr;
    char          num[6];
    unsigned char i, attempts;

    for (attempts = 0; attempts < 3; attempts++)
    {
        rx0_buff_len           = 0;
        Modem_Message_Recieved = FALSE;

        sprintf(ComBuf, "AT+CIPRXGET=4,1\r\n\0");
        UART0_WriteMsg(ComBuf);

        heat_time = T_QUERY;
        while (Modem_Message_Recieved == FALSE && heat_time > 0)
            ; /* wait for Timer0 to flag end-of-response */

        if (heat_time == 0)
        {
            delay_ms(100);
            continue;                   /* timed out — retry */
        }

        /* +CIPRXGET: 4,1,<count> */
        ptr = strstrf(RxUart0Buf, ": 4");
        if (ptr != NULL)
        {
            ptr += 6;                   /* skip ": 4,1," — point to <count> */
            i = 0;
            while (*ptr >= '0' && *ptr <= '9' && i < 5)
                num[i++] = *ptr++;
            num[i] = '\0';
            return (unsigned int)atoi(num);
        }

        delay_ms(500); //wait before retrying
    }

    return 0;   /* no data or query failed */
}

/*
  modem_read_chunk — Request up to max_bytes from the modem TCP buffer.
  The UART0 RX ISR fills RxUart0Buf[].  On success, *payload_out is set to
  the first byte of the actual file payload (immediately after the modem AT
  response header line), and the function returns the payload byte count.

  Command:  AT+CIPRXGET=2,1,<max_bytes>
  Response: +CIPRXGET: 2,1,<actual>,<remain>\r\n<data...>\r\nOK\r\n

  Returns: actual payload bytes (may be less than max_bytes), or 0 on error.
*/
static unsigned int modem_read_chunk(unsigned int max_bytes,
                                     char       **payload_out)
{
    char         *ptr;
    char          num[6];
    unsigned char i;
    unsigned int  actual;

    itoa(max_bytes, num);

    rx0_buff_len           = 0;
    Modem_Message_Recieved = FALSE;

    sprintf(ComBuf, "AT+CIPRXGET=2,1,%s\r\n\0", num);
    UART0_WriteMsg(ComBuf);

    heat_time = T_CHUNK;
    while (Modem_Message_Recieved == FALSE && heat_time > 0)
        ;
    if (heat_time == 0)
        return 0;

    RxUart0Buf[rx0_buff_len] = '\0';    /* safe string terminator */

    /* ── Parse "+CIPRXGET: 2,1,<actual>,<remain>" ── */
    ptr = strstrf(RxUart0Buf, ": 2");
    if (ptr == NULL)
        return 0;

    ptr += 6;                           /* skip ": 2,1," — now at <actual> */
    i = 0;
    while (*ptr >= '0' && *ptr <= '9' && i < 5)
        num[i++] = *ptr++;
    num[i] = '\0';
    actual = (unsigned int)atoi(num);
    if (actual == 0)
        return 0;

    /* ── Advance past the rest of the header line to the payload ── */
    while (*ptr && *ptr != '\n')
        ptr++;
    if (!*ptr)
        return 0;
    ptr++;                              /* point to the first byte of payload */

    *payload_out = ptr;

    /*
      Guard against premature Timer0 idle-fire: the modem declares <actual>
      bytes in its response header, but a brief TCP-segment gap can cause the
      UART idle timer to fire before all bytes have arrived.  Clamping to the
      bytes actually present in RxUart0Buf prevents scan_chunk() from walking
      into stale data and corrupting a partially-assembled row.
    */
    {
        unsigned int avail = rx0_buff_len - (unsigned int)(ptr - RxUart0Buf);
        if (avail < actual)
            actual = avail;
    }

    return actual;
}

/*
  send_fota_request — Transmit the "START_FOTA" command to the server
  via the existing CIP-SEND mechanism.
  Returns 1 on confirmed delivery, 0 on failure.
*/
static char send_fota_request(void)
{
    char FOTA_REQ[] = "a\r\nSTART_FOTA\r\n\0";
    char ok;

    CIPSEND_Lengh(15);
    UART0_WriteMsg(FOTA_REQ);

    ok = ChkNodemRespons(": 1,15,15", "error", 10);
    return (ok == 1) ? 1 : 0;
}


/*==============================================================================
  SECTION 6 — FOTA FILE HEADER PARSER
==============================================================================*/

/*
  parse_fota_header — Extract the vertical checksum target and file type from
  the proprietary header that precedes the Intel HEX data.

  'buf' must point to the first byte of the file body, which looks like:

      "98432\r\n<3D85>[1]:0A000000...\r\n..."
       ^file_size    ^vert_cs^type^first hex row

  On success:
    - Sets vert_cs_target and fota_file_type.
    - Returns a pointer to the ':' of the first Intel HEX row.

  On failure:
    - Prints a debug message and returns NULL.
*/
static char *parse_fota_header(char *buf)  //return pointer to the ':' of the first Intel HEX row
{
    char         *p = buf;    //pointer to the first byte of the file body
    char          tmp[8];    //temporary buffer to store the file size
    unsigned char i;
    unsigned int FileSize;

    /* Step 1: Display file-size line (informational) */
    i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < 7)  //copy the file size to the temporary buffer
        tmp[i++] = *p++;
    tmp[i] = '\0'; 
    sprintf(ComBuf, "File sz: %s ASCII bytes\r\n\0", tmp);
    UART1_WriteMsg(ComBuf);
    
    FileSize = (unsigned int)atoi(tmp);

    /* Advance past the \r\n that terminates the file-size line */
    while (*p == '\r' || *p == '\n')  //advance the pointer to the next line
        p++;

    /* ── Step 2: Find '<' that opens the vertical checksum field ── */
    while (*p && *p != '<' && *p != ':')
        p++;

    if (*p != '<')
    {
        SendDebugMsg("Header: '<' not found!\r\n\0");
        return NULL;
    }
    p++;    /* skip '<' */

    /* ── Step 3: Read 4 hex digits of vertical checksum ── */
    if (!p[0] || !p[1] || !p[2] || !p[3])  //check if the vertical checksum is truncated -4 hex digits
    {
        SendDebugMsg("Header: checksum truncated!\r\n\0");
        return NULL;
    }

    vert_cs_target = ((unsigned int)hex_nibble(p[0]) << 12)  //convert the 4 hex digits to a 16-bit vertical checksum
                   | ((unsigned int)hex_nibble(p[1]) <<  8)
                   | ((unsigned int)hex_nibble(p[2]) <<  4)
                   |  (unsigned int)hex_nibble(p[3]);
    p += 4;

    if (*p != '>')  //check if the VCS closing character is missing
    {
        SendDebugMsg("Header: missing '>'!\r\n\0");
        return NULL;
    }
    p++;    /* skip '>' */

    /* ── Step 4: Read file type from [T] ── */
    if (*p != '[')
    {
        SendDebugMsg("Header: missing '['!\r\n\0");
        return NULL;
    }
    p++;    /* skip '[' */   
    
    if(FileSize < 1500)    //eeprom file type
    fota_file_type = 2;
    else
    fota_file_type = (unsigned char)(*p - '0');     /* '1'→1, '2'→2 */
    p++; 
    
    

    if (*p != ']')
    {
        SendDebugMsg("Header: missing ']'!\r\n\0");
        return NULL;
    }
    p++;    /* skip ']' — now pointing at ':' of first Intel HEX row */

    if (*p != ':')
    {
        SendDebugMsg("Header: no first row start!\r\n\0");
        return NULL;
    }

    sprintf(ComBuf, "FOTA hdr OK: VCS=0x%04X - File type=%d\r\n\0",
            vert_cs_target, (int)fota_file_type);
    UART1_WriteMsg(ComBuf);

    return p;   /* ':' of the first Intel HEX row */
}


/*==============================================================================
  SECTION 7 — SEQUENTIAL EEPROM READER  (used by ValidityCheck)
==============================================================================*/

/*
  rd_init — Seed the sequential EEPROM reader at absolute byte position
  'start_addr' (within block 0).

  Because twiReadEEP1Byte() both sets the internal address AND returns the
  byte at that address, we save that first byte in rd_seed and return it on
  the very first call to rd_byte().  Every subsequent call uses the fast
  ReadEEPCurrentAddress() (which auto-increments) for block 0, and uses
  twiReadEEP1Byte() for block 1 (addressed-read, slightly slower but correct —
  ReadEEPCurrentAddress always uses I2C address 0xA0 regardless of block).
*/
static void rd_init(unsigned int start_addr)
{
    rd_pos   = (long)start_addr;
    rd_seed  = (unsigned char)twiReadEEP1Byte(0, start_addr);
    rd_first = 1;
}

/*
  rd_byte — Return the byte at rd_pos and advance rd_pos by one.
  Handles block crossing transparently.
*/
static unsigned char rd_byte(void)
{
    unsigned char b;

    #asm("wdr");                        /* keep watchdog satisfied on long reads */

    /* First call after rd_init: return the pre-fetched byte */
    if (rd_first)
    {
        rd_first = 0;
        rd_pos++;
        return rd_seed;
    }

    if (rd_pos < BLOCK_BYTES)
    {
        /*
          Block 0: ReadEEPCurrentAddress() issues a read-current-address I2C
          cycle (no address resend required) and returns the next byte.
          Very fast — only one I2C transaction per byte.
        */
        b = (unsigned char)ReadEEPCurrentAddress();
    }
    else
    {
        /*
          Block 1: ReadEEPCurrentAddress() always targets I2C address 0xA0
          (block 0), so we must use twiReadEEP1Byte() with the correct block-1
          address.  This is slower (full random-read I2C sequence per byte)
          but correct.
        */
        unsigned int addr1 = EEP_ADDR(rd_pos);   /* offset within block 1 */
        b = (unsigned char)twiReadEEP1Byte(1, addr1);
    }

    rd_pos++;
    return b;
}

/*
  rd_word — Read two sequential bytes and combine them as a big-endian word.
*/
static unsigned int rd_word(void)
{
    unsigned int w;
    w  = (unsigned int)rd_byte() << 8;
    w |= rd_byte();
    return w;
}


/*==============================================================================
  SECTION 8 — PUBLIC: RxUpdateFile
==============================================================================*/

/*
  RxUpdateFile — Main FOTA reception function.

  Orchestrates the complete receive-convert-store pipeline:
    1. Initialise all module state.
    2. Switch modem to buffered (on-demand) receive mode.
    3. Send "START_FOTA" request to server.
    4. Read the first chunk, parse the proprietary file header.
    5. Store the 3-byte EEPROM header ('[', file_type, ']').
    6. Loop: pull chunks from the modem, scan for complete Intel HEX rows,
       convert each row to binary, and write to EEPROM 128 bytes at a time.
    7. Flush the last partial page on EOF.
    8. Re-enable UART1 (BLE) and return.

  Returns: total EEPROM bytes written (> 0 = success), or 0 on any failure.
  The caller (modem_manager.c) treats non-zero as success and then calls
  ValidityCheck(0).
*/
unsigned int RxUpdateFile(void)
{
    unsigned int  available, actual;
    char         *payload, *hex_start;
    unsigned int  result;

    /* ── Initialise all module state ────────────────────────────────────── */
    pg_fill        = 0;
    pg_pos         = 0L;
    row_fill       = 0;
    in_row         = 0;
    vert_cs_target = 0;
    vert_cs_calc   = 0;
    rows_stored    = 0;
    fota_file_type = 0;
    eof_stored     = 0;

    rx0_buff_len           = 0;
    rx_counter0            = 0;
    rx_counter1            = 0;
    UpdateSession          = FALSE;
    FirmwareUpdateTime     = FALSE;
    Modem_Message_Recieved = FALSE;

    /* ── System setup ────────────────────────────────────────────────────── */
    #asm("sei");
    DISABLE_RX_INT_UART1();             /* BLE (UART1) not needed during FOTA    */
    ENABLE_TIMER1();                    /* start heat_time countdown (100 ms/tick)*/

    SendDebugMsg("\r\n=== FOTA RX START ===\r\n\0");

    /* ── Put modem into buffered receive mode ─────────────────────────────── */
    modem_set_buffered();

    /* ── Request the FOTA file from the server ───────────────────────────── */
    heat_time = T_INIT;
    if (!send_fota_request())
    {
        SendDebugMsg("FOTA request FAILED.\r\n\0");
        goto rx_fail;
    }
    SendDebugMsg("FOTA request sent OK.\r\n\0");

    /* ── Wait until the server starts sending data ───────────────────────── */
    FirmwareUpdateTime = TRUE;
    heat_time          = T_INIT;

    do
    {
        #asm("wdr");
        delay_ms(500);
        available = modem_query_available();
    }
    while (available == 0 && heat_time > 0);

    if (available == 0)
    {
        SendDebugMsg("Timeout: no server data.\r\n\0");
        goto rx_fail;
    }

    /*==========================================================================
      FIRST CHUNK — contains the proprietary header + start of hex rows
    ==========================================================================*/

    heat_time = T_CHUNK;
    actual = modem_read_chunk(CHUNK_MAX, &payload);
    if (actual == 0)
    {
        SendDebugMsg("First chunk FAILED.\r\n\0");
        goto rx_fail;
    }

    /* ── Parse proprietary FOTA header ────────────────────────────────────── */
    hex_start = parse_fota_header(payload);
    if (hex_start == NULL)
        goto rx_fail;

    /* ── Write 3-byte EEPROM file-type header  '[', type, ']' ─────────────── */
    if (!store_byte('['))             goto rx_fail;
    if (!store_byte(fota_file_type)) goto rx_fail;
    if (!store_byte(']'))             goto rx_fail;

    /*
      Scan the Intel HEX rows present in the first chunk.
      The payload pointer 'hex_start' is within RxUart0Buf[]; the remaining
      byte count is the total payload minus whatever the header consumed.
    */
    actual -= (unsigned int)(hex_start - payload);
    if (!scan_chunk(hex_start, actual))
        goto rx_fail;

    if (eof_stored)
        goto rx_done;

    LED1_ON;

    /*==========================================================================
      MAIN RECEIVE LOOP — pull subsequent chunks until the EOF record is seen
    ==========================================================================*/
    do
    {
        #asm("wdr");
        delay_ms(20);                   /* brief pause — let modem buffer refill  */

        available = modem_query_available();

        if (available == 0)
        {
            if (heat_time == 0)
            {
                SendDebugMsg("Chunk timeout!\r\n\0");
                goto rx_fail;
            }
            heat_time = T_CHUNK;        /* reset the watchdog for this cycle      */
            delay_ms(200);
            continue;
        }

        heat_time = T_CHUNK;
        actual = modem_read_chunk(CHUNK_MAX, &payload);

        if (actual == 0)
        {
            /* The modem reported data available but returned nothing.
               If we have already seen EOF this is harmless; otherwise abort. */
            if (eof_stored) break;
            SendDebugMsg("Empty chunk — aborting.\r\n\0");
            goto rx_fail;
        }

        if (!scan_chunk(payload, actual))
            goto rx_fail;

    }
    while (!eof_stored);

rx_done:
    /* ── Flush any remaining bytes in the page buffer ───────────────────── */
    if (!flush_page())
    {
        SendDebugMsg("Final EEPROM flush FAILED!\r\n\0");
        goto rx_fail;
    }

    LED1_OFF;
    FirmwareUpdateTime     = FALSE;
    UpdateSession          = FALSE;
    Modem_Message_Recieved = FALSE;
    rx0_buff_len           = 0;
    ENABLE_UART1();
    ServerResponseTimeOut  = 70;
    mainTask               = TASK_MODEM;

    result = (unsigned int)pg_pos;      /* total bytes written — non-zero = OK   */

    sprintf(ComBuf,
            "\r\n=== FOTA RX OK: %u rows, %u bytes ===\r\n\0",
            rows_stored, result);
    UART1_WriteMsg(ComBuf);

    return result;

rx_fail:
    LED1_OFF;
    FirmwareUpdateTime     = FALSE;
    UpdateSession          = FALSE;
    Modem_Message_Recieved = FALSE;
    rx0_buff_len           = 0;
    ENABLE_UART1();
    mainTask = TASK_MODEM;
    SendDebugMsg("\r\n=== FOTA RX FAILED ===\r\n\0");
    return 0;
}


/*==============================================================================
  SECTION 9 — PUBLIC: ValidityCheck
==============================================================================*/

/*
  ValidityCheck — Re-read the binary Intel HEX data from EEPROM, verify every
  row's checksum, and confirm the accumulated vertical checksum.

  Algorithm:
    1. Seed sequential EEPROM reader at 'start_addr'.
    2. Skip the 3-byte file header ('[', file_type, ']').
    3. For each Intel HEX record:
         a. Read ':'  byte_count  addr_H  addr_L  rec_type
         b. If EOF  (rec_type == 1): break.
         c. Accumulate sum = byte_count + addr_H + addr_L + rec_type + Σdata
         d. Read data bytes, add each to sum.
         e. Read checksum byte; verify (sum + checksum) mod 256 == 0.
         f. Add checksum byte to vert_cs_verify.
    4. Compare vert_cs_verify against vert_cs_target  (set by RxUpdateFile).

  Note on checksum arithmetic:
    The Intel HEX standard says the checksum is the two's complement of the
    sum of all other bytes, so:  (sum_of_all_bytes_including_CS) mod 256 == 0.
    Equivalently: CS = (~sum_before_CS + 1) & 0xFF = (0x100 - sum_before_CS).
    Both expressions are equivalent; we use the simpler "sum == 0" check here.

  'start_addr'  — EEPROM start address (0 for a freshly written firmware).
  Returns       — 0 = VALID,  99 = any error.
*/
int ValidityCheck(unsigned int start_addr)
{
    unsigned char  byte_count, rec_type, row_cs, db;
    unsigned int   load_addr;
    unsigned int   vert_cs_verify;
    unsigned int   row_num;
    unsigned char  k, sum;
    unsigned char  b;
    char           eof_seen;
    char           skip;

    SendDebugMsg("\r\n--- Validity Check ---\r\n\0");

    vert_cs_verify = 0;
    row_num        = 0;
    eof_seen       = 0;

    /* ── Seed the sequential EEPROM reader ───────────────────────────────── */
    rd_init(start_addr);

    /* ── Skip the 3-byte file header: '[', file_type, ']' ───────────────── */
    /*
      The 3-byte header stored by RxUpdateFile is:
        Byte 0: '[' (0x5B)
        Byte 1: file_type  (0x01 = flash)
        Byte 2: ']' (0x5D)
      We scan until we find ']', allowing a small window in case of any
      alignment issue.
    */
    skip = 8;
    do
    {
        b = rd_byte();
        skip--;
    }
    while (b != ']' && skip > 0);

    if (b != ']')
    {
        SendDebugMsg("Validity: file header not found!\r\n\0");
        return 99;
    }

    /* ── Process Intel HEX records ──────────────────────────────────────── */
    do
    {
        #asm("wdr");                    /* keep watchdog happy on large files     */

        /* Every record must start with ':' (0x3A) */
        b = rd_byte();
        if (b != 0x3A)
        {
            sprintf(ComBuf, "\r\nRow %u: expected 0x3A, got 0x%02X\r\n\0",
                    row_num + 1, (unsigned int)b);
            UART1_WriteMsg(ComBuf);
            return 99;
        }

        /* Fixed 4-byte record header */
        byte_count = rd_byte();         /* LL   */
        load_addr  = rd_word();         /* AAAA  (big-endian) */
        rec_type   = rd_byte();         /* TT   */

        /* ── EOF record ── */
        if (rec_type == 0x01)
        {
            rd_byte();                  /* consume the 0xFF checksum byte         */
            row_num++;
            eof_seen = 1;
            SendDebugMsg("EOF record OK.\r\n\0");
            break;
        }

        /* Build running checksum over all bytes in this record */
        sum = byte_count
            + (unsigned char)(load_addr >> 8)
            + (unsigned char)(load_addr & 0xFF)
            + rec_type;

        /* ── Read and sum the data bytes ── */
        for (k = 0; k < byte_count; k++)
        {
            db   = rd_byte();
            sum += db;
        }

        /* ── Read and verify the row checksum byte ── */
        row_cs  = rd_byte();
        sum    += row_cs;

        row_num++;

        /* Progress indicator every 50 rows */
        if ((row_num % 50) == 0)
        {
            LED1_ON;
            _putchar1('~');
        }
        else
        {
            LED1_OFF;
        }

        /* Check: (sum of all record bytes including CS) must be 0 mod 256 */
        if (sum != 0x00)
        {
            sprintf(ComBuf, "\r\nRow %u CS ERR: sum=0x%02X\r\n\0",
                    row_num, (unsigned int)sum);
            UART1_WriteMsg(ComBuf);
            return 99;
        }

        /* Only data records (type 0x00) contribute to vertical checksum */
        vert_cs_verify += (unsigned int)row_cs;

    }
    while (!eof_seen);

    LED1_OFF;

    /* ── Final vertical checksum comparison ──────────────────────────────── */
    if (vert_cs_verify == vert_cs_target)
    {
        sprintf(ComBuf,
                "\r\nVALID: %u rows, vert_cs=0x%04X OK\r\n\0",
                row_num - 1,            /* -1: EOF row not counted as data row   */
                vert_cs_verify);
        UART1_WriteMsg(ComBuf);
        return 0;                       /* SUCCESS */
    }
    else
    {
        sprintf(ComBuf,
                "\r\nINVALID: calc=0x%04X expected=0x%04X (%u rows)\r\n\0",
                vert_cs_verify, vert_cs_target, row_num - 1);
        UART1_WriteMsg(ComBuf);
        return 99;                      /* FAILURE */
    }
}


/*==============================================================================
  SECTION 10 — MEMORY SELF-TEST  (keep for hardware bringup / debugging)
==============================================================================*/

void MemoryReadTest(char block, unsigned int start, unsigned int end)
{
    unsigned char k;
    unsigned int  i;

    SendDebugMsg("Memory Read Test:\r\n\0");

    k = (unsigned char)twiReadEEP1Byte(block, start);
    _putchar1((char)k);
    delay_ms(5);

    for (i = start + 1; i < end; i++)
    {
        k = (unsigned char)ReadEEPCurrentAddress();
        _putchar1((char)k);
        delay_ms(5);
    }

    putchar1('\r');
    putchar1('\n');
}

void TestMemory(void)
{
    unsigned char i;
    char data = (char)0xA5;

    if (!twiWriteExtMem1Byte(0, 0x0000, data))
    {
        SendDebugMsg("Memory Write FAILED.\r\n\0");
        return;
    }

    delay_ms(50);
    i = (unsigned char)twiReadEEP1Byte(0, 0);

    if (i == 0xA5)
        SendDebugMsg("Memory Write/Read OK.\r\n\0");
    else
        SendDebugMsg("Memory Read MISMATCH.\r\n\0");
}

/*──────────────────────────────────────────────────────────────────────────────
  ShowHexString — small debug helper (declared here to match original .cci)
──────────────────────────────────────────────────────────────────────────────*/
//void ShowHexString(unsigned char *message, char length)
//{
//    char i;
//    for (i = 0; i < length; i++)
//    {
//        sprintf(ComBuf, "%02X ", (unsigned int)message[i]);
//        UART1_WriteMsg(ComBuf);
//    }
//    UART1_WriteMsg("\r\n\0");
//}
