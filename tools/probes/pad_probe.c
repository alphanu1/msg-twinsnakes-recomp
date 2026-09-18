/* Live controller probe.
 *
 * Confirms the mapping end to end against real hardware, which the unit tests
 * deliberately cannot: they exercise the pure layer with synthesised input so
 * they run without a controller. This closes that gap when one is plugged in.
 *
 *   pad_probe              one reading
 *   pad_probe --watch      continuous, until Ctrl-C
 *   pad_probe --sample N   record the extremes reached over N seconds
 *
 * --sample exists because the live display overwrites itself with a carriage
 * return, which is unreadable once piped. Recording extremes also answers the
 * question that matters - did every axis reach its full range, and did the
 * trigger click latch - rather than what the stick happened to be doing at
 * the instant the last frame printed.
 */
#include "pad/pad.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int mgs_input_init(void);
void mgs_input_shutdown(void);
void mgs_input_poll(void);
void mgs_input_read(unsigned port, MgsPadState* out);
unsigned mgs_input_connected_count(void);

static void print_buttons(uint16_t b)
{
    struct { uint16_t bit; const char* name; } map[] = {
        {PAD_BUTTON_A,"A"}, {PAD_BUTTON_B,"B"}, {PAD_BUTTON_X,"X"},
        {PAD_BUTTON_Y,"Y"}, {PAD_BUTTON_START,"START"}, {PAD_TRIGGER_Z,"Z"},
        {PAD_TRIGGER_L,"L-click"}, {PAD_TRIGGER_R,"R-click"},
        {PAD_BUTTON_UP,"up"}, {PAD_BUTTON_DOWN,"down"},
        {PAD_BUTTON_LEFT,"left"}, {PAD_BUTTON_RIGHT,"right"},
    };
    size_t i; int first = 1;
    if (!b) { printf("-"); return; }
    for (i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (b & map[i].bit) { printf("%s%s", first ? "" : "+", map[i].name); first = 0; }
}

int main(int argc, char** argv)
{
    int watch = argc > 1 && strcmp(argv[1], "--watch") == 0;
    int sample = argc > 2 && strcmp(argv[1], "--sample") == 0;
    int seconds = sample ? atoi(argv[2]) : 0;
    unsigned n;

    if (!mgs_input_init()) { printf("SDL3 gamepad init failed\n"); return 1; }
    mgs_input_poll();
    n = mgs_input_connected_count();
    printf("gamepads connected: %u\n", n);
    if (!n) { printf("nothing plugged in\n"); mgs_input_shutdown(); return 1; }

    if (sample) {
        MgsPadState p;
        int lx_min=0, lx_max=0, ly_min=0, ly_max=0;
        int cx_min=0, cx_max=0, cy_min=0, cy_max=0;
        int tl_max=0, tr_max=0;
        uint16_t seen = 0u;
        int i, iterations = seconds * 20;

        printf("sampling for %d seconds - move everything\n", seconds);
        for (i = 0; i < iterations; ++i) {
            mgs_input_poll();
            mgs_input_read(0, &p);
            if (p.stick_x < lx_min) lx_min = p.stick_x;
            if (p.stick_x > lx_max) lx_max = p.stick_x;
            if (p.stick_y < ly_min) ly_min = p.stick_y;
            if (p.stick_y > ly_max) ly_max = p.stick_y;
            if (p.substick_x < cx_min) cx_min = p.substick_x;
            if (p.substick_x > cx_max) cx_max = p.substick_x;
            if (p.substick_y < cy_min) cy_min = p.substick_y;
            if (p.substick_y > cy_max) cy_max = p.substick_y;
            if (p.trigger_l > tl_max) tl_max = p.trigger_l;
            if (p.trigger_r > tr_max) tr_max = p.trigger_r;
            seen |= p.button;
            usleep(50000);
        }

        printf("\nextremes reached (SDK range is +/-%d stick, +/-%d c-stick):\n",
               PAD_STICK_RANGE, PAD_SUBSTICK_RANGE);
        printf("  stick X   %4d .. %4d\n", lx_min, lx_max);
        printf("  stick Y   %4d .. %4d\n", ly_min, ly_max);
        printf("  c-stick X %4d .. %4d\n", cx_min, cx_max);
        printf("  c-stick Y %4d .. %4d\n", cy_min, cy_max);
        printf("  trigger L    0 .. %4d   (click latches at %d)\n", tl_max, PAD_TRIGGER_CLICK_POINT);
        printf("  trigger R    0 .. %4d\n", tr_max);
        printf("  buttons seen: "); print_buttons(seen); printf("\n");
        mgs_input_shutdown();
        return 0;
    }

    for (;;) {
        MgsPadState p;
        mgs_input_poll();
        mgs_input_read(0, &p);

        printf("\rstick(%4d,%4d)  c-stick(%4d,%4d)  L%3u R%3u  buttons: ",
               p.stick_x, p.stick_y, p.substick_x, p.substick_y,
               p.trigger_l, p.trigger_r);
        print_buttons(p.button);
        printf("                    ");
        fflush(stdout);

        if (!watch) { printf("\n"); break; }
        usleep(50000);
    }

    mgs_input_shutdown();
    return 0;
}
