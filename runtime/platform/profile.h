#ifndef MGS_PROFILE_H
#define MGS_PROFILE_H

/* Statistical profiler over process CPU time. MGS_PROFILE=1 turns it on. */
void mgs_profile_start(void);
void mgs_profile_report(void);
void mgs_profile_this_thread(void);
/* MGS_PROFILE_OCC=1: which guest thread was current, per second of wall
 * time, read from the guest's __OSCurrentThread at every sample. */
void mgs_profile_set_guest_ram(const unsigned char* ram);

#endif
