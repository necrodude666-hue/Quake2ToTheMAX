/* Win98/VS2005 WinMM cleanup. Retain driver-owned buffers on failure. */
#ifndef BDGN_AUDIO_CLEANUP_H
#define BDGN_AUDIO_CLEANUP_H
static qboolean Audio_CloseWave (HWAVEOUT *device, WAVEHDR *headers, int count, const char *owner)
{
 MMRESULT result;
 DWORD deadline;
 int i;
 qboolean pending;
 if (!*device) return true;
 result = waveOutReset (*device);
 if (result != MMSYSERR_NOERROR)
 {
  Com_Printf ("%s: waveOutReset failed (%u); retaining device\n", owner, (unsigned)result);
  return false;
 }
 deadline = timeGetTime () + 500;
 do
 {
  pending = false;
  for (i = 0; headers && i < count; i++)
  {
   if (!(headers[i].dwFlags & WHDR_PREPARED)) continue;
   result = waveOutUnprepareHeader (*device, &headers[i], sizeof(WAVEHDR));
   if (result == WAVERR_STILLPLAYING) pending = true;
   else if (result != MMSYSERR_NOERROR)
   {
    Com_Printf ("%s: waveOutUnprepareHeader failed (%u); retaining buffers\n", owner, (unsigned)result);
    return false;
   }
  }
  if (!pending) break;
  Sleep (1);
 } while ((LONG)(timeGetTime () - deadline) < 0);
 if (pending)
 {
  Com_Printf ("%s: headers still owned by driver; retaining device and buffers\n", owner);
  return false;
 }
 result = waveOutClose (*device);
 if (result != MMSYSERR_NOERROR)
 {
  Com_Printf ("%s: waveOutClose failed (%u); retaining device\n", owner, (unsigned)result);
  return false;
 }
 *device = NULL;
 return true;
}
#endif
