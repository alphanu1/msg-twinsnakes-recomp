#ifndef MGS_PROFILE_H
#define MGS_PROFILE_H

/* Statistical profiler over process CPU time. MGS_PROFILE=1 turns it on. */
void mgs_profile_start(void);
void mgs_profile_report(void);

#endif
