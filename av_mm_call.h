#ifndef __av_mm_call_h__
#define __av_mm_call_h__

void av_mm_call_register(AvModem *m, MMCall *call);
void av_mm_call_unregister(AvModem *m, const gchar *call_path);
void av_mm_call_release_mmcalls(AvModem *m);
void av_mm_call_sipcall(AvModem *m, const char *dest_number);

#endif
