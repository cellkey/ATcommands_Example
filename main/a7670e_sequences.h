/**
 * @file a7670e_sequences.h
 * @brief A7670E modem init and network sequences (table-driven)
 *
 * Matches SIMCOM_A7670E_MODEM_INIT&TCP_CONNECTION.log order.
 * Phase 1: basic init (AT through CGDCONT).
 * Phase 2: network (NETCLOSE → NETOPEN → IPADDR).
 */

#ifndef A7670E_SEQUENCES_H
#define A7670E_SEQUENCES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run full A7670E init: phase 1 (basic init) then phase 2 (network open).
 * @return Number of successful commands in phase 1, or -1 on critical failure.
 */
int run_a7670e_init(void);

/**
 * @brief Connect to server, send GET (72+81 bytes), wait for 200 OK, set CIPRXGET=1.
 * Call after run_a7670e_init().
 * @return 0 on success, -1 on failure.
 */
int run_a7670e_connect_and_register(void);

/**
 * @brief Close link 1 and reconnect (CIPOPEN + GET + 200 OK + CIPRXGET=1). Use when KA not ACKed after retry.
 * @return 0 on success, -1 on failure.
 */
int run_a7670e_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* A7670E_SEQUENCES_H */
