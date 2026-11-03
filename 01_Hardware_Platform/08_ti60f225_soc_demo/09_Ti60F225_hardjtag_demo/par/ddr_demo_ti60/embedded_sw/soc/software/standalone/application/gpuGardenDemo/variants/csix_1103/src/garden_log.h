/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GARDEN_LOG_H
#define GARDEN_LOG_H

#define GARDEN_LOG_QUEUE_BYTES 2880U
#define GARDEN_LOG_FORMAT_BYTES 832U

typedef struct {
    unsigned queued;          /* Bytes accepted but not yet passed to tx_put. */
    unsigned high_water;      /* Largest queued byte count since startup. */
    unsigned dropped_lines;   /* Whole calls refused: queue full/no sync sink. */
    unsigned truncated_lines; /* Whole calls refused: formatting error/too long. */
} GardenLogStats;

/* Single producer/pump, no interrupts or other concurrent UART writers.
 * tx_free must report available FIFO slots; tx_put must consume exactly one.
 * Binding/rebinding preserves pending bytes. Neither bind nor set_async sends.
 */
void garden_log_bind(unsigned (*tx_free)(void), void (*tx_put)(char));
void garden_log_set_async(unsigned enabled);

/* Format one record (normally one CRLF-terminated line), without adding CRLF.
 * Returns formatted bytes, or -1 if the entire record was refused.
 * ASYNC accepts all bytes together or none. SYNC uses tx_put for every byte.
 * After ASYNC->SYNC, the next printf first drains accepted pending bytes using
 * tx_put so newer output cannot overtake them. That explicit SYNC call may block.
 * No float formatting is used by firmware; format strings must match arguments.
 */
int garden_log_printf(const char *fmt, ...);

/* Read FIFO availability once and transmit at most that many pending bytes.
 * Never wait for FIFO space. Works in both modes to drain pending ASYNC output.
 * A budget of zero leaves both the queue and callbacks' output unchanged.
 */
void garden_log_pump(void);
const GardenLogStats *garden_log_stats(void);

#endif
