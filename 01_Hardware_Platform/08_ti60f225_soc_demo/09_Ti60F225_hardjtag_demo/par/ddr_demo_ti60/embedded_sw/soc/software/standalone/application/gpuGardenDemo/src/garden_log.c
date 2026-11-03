/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "garden_log.h"
#include <limits.h>
#include <stdarg.h>
#include <string.h>

static char queue[GARDEN_LOG_QUEUE_BYTES];
static char formatted[GARDEN_LOG_FORMAT_BYTES];
static unsigned head, tail, asynchronous;
static unsigned (*fifo_free)(void);
static void (*fifo_put)(char);
static GardenLogStats stats;

void garden_log_bind(unsigned (*tx_free)(void), void (*tx_put)(char))
{
    fifo_free = tx_free;
    fifo_put = tx_put;
}

void garden_log_set_async(unsigned enabled)
{
    asynchronous = enabled != 0U;
}

static void send_pending(unsigned budget)
{
    if (budget > stats.queued) budget = stats.queued;
    while (budget != 0U) {
        fifo_put(queue[head]);
        if (++head == GARDEN_LOG_QUEUE_BYTES) head = 0U;
        stats.queued--;
        budget--;
    }
}

/* Integer-only formatter: never pull libc's printf/float machinery into RAM.
 * Width is an optional zero-padded width 0..32 for numeric conversions only.
 * A failed conversion refuses the whole record before any FIFO/ring mutation.
 */
static int append_char(unsigned *count, char c)
{
    if (*count >= GARDEN_LOG_FORMAT_BYTES - 1U) return 0;
    formatted[(*count)++] = c;
    return 1;
}

static int format_record(const char *fmt, va_list args)
{
    unsigned count = 0U;
    if (!fmt) return -1;
    for (const char *cursor = fmt; *cursor; cursor++) {
        unsigned width = 0U, padded = 0U;
        unsigned value, base, digits_count = 0U, negative = 0U;
        char digits[sizeof(unsigned) * CHAR_BIT];
        if (*cursor != '%') {
            if (!append_char(&count, *cursor)) return -1;
            continue;
        }
        cursor++;
        if (*cursor == '0') {
            padded = 1U;
            cursor++;
            while (*cursor >= '0' && *cursor <= '9') {
                width = width * 10U + (unsigned)(*cursor - '0');
                if (width > 32U) return -1;
                cursor++;
            }
        }
        if (*cursor == '%' && !padded) {
            if (!append_char(&count, '%')) return -1;
            continue;
        }
        if (*cursor == 'c' && !padded) {
            if (!append_char(&count, (char)va_arg(args, int))) return -1;
            continue;
        }
        if (*cursor == 's' && !padded) {
            const char *text = va_arg(args, const char *);
            if (!text) text = "(null)";
            while (*text) {
                if (!append_char(&count, *text++)) return -1;
            }
            continue;
        }
        base = 10U;
        if (*cursor == 'd') {
            int signed_value = va_arg(args, int);
            negative = signed_value < 0;
            value = negative ? 0U - (unsigned)signed_value : (unsigned)signed_value;
        } else if (*cursor == 'u' || *cursor == 'x') {
            value = va_arg(args, unsigned);
            if (*cursor == 'x') base = 16U;
        } else {
            return -1; /* Includes unfinished %, lengths, floats and flags. */
        }
        do {
            digits[digits_count++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value != 0U);
        if (negative && !append_char(&count, '-')) return -1;
        while (width > digits_count + negative) {
            if (!append_char(&count, '0')) return -1;
            width--;
        }
        while (digits_count != 0U) {
            if (!append_char(&count, digits[--digits_count])) return -1;
        }
    }
    formatted[count] = '\0';
    return (int)count;
}

int garden_log_printf(const char *fmt, ...)
{
    va_list args;
    int length;
    unsigned count, first;

    va_start(args, fmt);
    length = format_record(fmt, args);
    va_end(args);
    if (length < 0 || (unsigned)length >= sizeof(formatted)) {
        stats.truncated_lines++;
        return -1;
    }
    count = (unsigned)length;
    if (!asynchronous) {
        if (!fifo_put) {
            stats.dropped_lines++;
            return -1;
        }
        send_pending(stats.queued);
        for (unsigned i = 0U; i < count; i++) fifo_put(formatted[i]);
        return length;
    }
    if (count > GARDEN_LOG_QUEUE_BYTES - stats.queued) {
        stats.dropped_lines++;
        return -1;
    }
    first = GARDEN_LOG_QUEUE_BYTES - tail;
    if (first > count) first = count;
    memcpy(queue + tail, formatted, first);
    memcpy(queue, formatted + first, count - first);
    tail += count;
    if (tail >= GARDEN_LOG_QUEUE_BYTES) tail -= GARDEN_LOG_QUEUE_BYTES;
    stats.queued += count;
    if (stats.queued > stats.high_water) stats.high_water = stats.queued;
    return length;
}

void garden_log_pump(void)
{
    if (!stats.queued || !fifo_free || !fifo_put) return;
    send_pending(fifo_free());
}

const GardenLogStats *garden_log_stats(void)
{
    return &stats;
}
