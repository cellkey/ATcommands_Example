/**
 * @file unit_ready.h
 * @brief One-shot "Unit Ready" after server path is up (Wi‑Fi: TCP 200 + CONNECTED; modem-only: init OK).
 */

#ifndef UNIT_READY_H
#define UNIT_READY_H

#ifdef __cplusplus
extern "C" {
#endif

/** Safe to call from multiple places; prints at most once per boot. */
void unit_ready_try_announce_once(void);

#ifdef __cplusplus
}
#endif

#endif /* UNIT_READY_H */
