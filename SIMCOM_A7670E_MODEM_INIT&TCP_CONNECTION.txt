    
 //Modem A7670E connection log   
 //AT commands and expected responses.   

             #Powerup or reset 
[43:49:465] #-----------monitoring---------
[43:49:466] #EazyGate by Creacell..
[43:49:467] #UNIT ID: cr18062022
            #Firmware version: 1.2.28
[43:49:472] #-----------------------------


[43:55:564] #EazyGate - Modem init..
[43:55:786] #Modem Ignition..delay 5 seconds

            AT
            OK
[44:02:845] AT+CSQ
[44:02:848] +CSQ: 22,0
[44:02:848] OK
[44:02:882] AT+CREG?
[44:02:885] +CREG: 0,1
[44:02:885] OK
[44:03:086] #Modem Registered..
[44:03:086] AT&F;E0
[44:03:090] OK
[44:03:186] AT+CFUN=1
[44:03:220] OK
[44:03:287] AT+CLIP=1                   //enable caller ID - modem ready to be called
[44:03:290] OK
[44:03:388] AT+CGMR
[44:03:393] +CGMR: A131B04A7670M6C
[44:03:393] OK
[44:03:490] AT+CGSN
[44:03:499] 862608083104098
[44:03:499] OK
[44:03:693] AT+CIPCCFG?                //can be ignored
[44:03:698] +CIPCCFG: 10,0,0,1,0,0,500 //can be ignored
[44:03:698] OK                         //can be ignored
[44:03:796] AT+CIPRXGET=0              //default-while init non buffered URC mode
[44:03:831] AT+COPS?
[44:03:835] +COPS: 0,2,"42501",0
[44:03:835] OK
[44:03:933] AT+CICCID
[44:03:938] +ICCID: 89972191200029939153  //SIM ID notification
[44:03:938] OK
[44:03:972] AT+CGATT?
[44:03:974] +CGATT: 1
[44:03:974] OK
[44:04:062] AT+CGACT=1,1
[44:04:062] OK
[44:04:097] AT+CIPMODE?
[44:04:099] +CIPMODE: 0      //Non transparent mode
[44:04:099] OK
[44:04:139] AT+CGDCONT=1,"IP","internet"
[44:04:139] K
[44:04:173] #Closing and re Opening NETWORK
[44:04:174] AT+NETCLOSE
[44:04:236] OK
[44:04:236] +CIPCLOSE: 1,0
[44:04:237] +NETCLOSE: 0
[44:04:377] AT+NETOPEN
[44:04:378] OK
[44:04:382] +NETOPEN: 0
[44:04:417] #CHK IP..
[44:04:418] AT+IPADDR
[44:04:422] +IPADDR: 100.105.134.126
[44:04:422] OK
            AT+CIPOPEN=1,"TCP","gates.crea-cell.com",3000 //dialing to server-TCP connection
[44:04:528] OK
            AT+CIPSEND: 1,72                //send GET message to modem - first part- unit ID-72 bytes
            >GET /api/device/cr18061950/listen HTTP/1.1  
[44:07:114] Transfer-Encoding: chunked
[44:07:117] OK
[44:07:117] +CIPSEND: 1,72,72         //72 bytes of GET message has been sent

[44:07:167] AT+CIPSEND=1,81           //GET message part 2. some data used by server - 81 bytes
[44:07:206] >FW_VERSION: 1.2.28      //firmware version notification
[44:07:209] COPS_ID: 42501****       //operator ID notification
[44:07:212] CSQ: 22                 //signal quality notification
[44:07:218] SIM_ID: 89972191200029939153 //SIM ID notification
[44:07:221] OK
[44:07:221] +CIPSEND: 1,81,81       //last 81 bytes of GET message sent

[44:07:420] #Wait for 200..  //wait for HTTP response from server
[44:07:893]
[44:07:893] RECV FROM:178.62.86.70:3000   
[44:07:893] +IPD176                       //HTTP response from servers-176 bytes
[44:07:893] HTTP/1.1 200 OK
[44:07:893] X-Powered-By: Express
[44:07:894] Content-Language: en
[44:07:894] Transfer-Encoding: chunked
[44:07:894] Date: Tue, 10 Feb 2026 12:44:07 GMT
[44:07:895] Connection: keep-alive
[44:07:895] Keep-Alive: timeout=5
[44:07:895]
[44:07:927] #Got 200..

[44:07:929] AT+CIPRXGET=1  //now change to buffered URC 
[44:07:930] OK
[44:08:332] #In main loop..ready
 
 ; to add after connection is established and code debuged:

 ; 1. Send Periodic KEEP ALIVE message. Keep TCP connection alive.
;  2. Reconnect to server if connection is lost. (No ACK response from server to KA)
;  3. Receive commands from server, parse it and execute (see List of availsbe commands)
;  4. Send ACK response to server after command execution.
;  5. receive a call, dialed by user, get caller ID and notify server with caller ID.
;     Get server command to execute if caller ID is in the list o  f allowed   callers.
;  6.  to be defined later.
 
 