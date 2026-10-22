/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The GMU firmware planner over a650_gmu.bin (not in Git, path on the command line): the a650 family regions of
 * a6xx_gmu_fw_load (ITCM at 0, the icache object at 0x4000, DTCM at 0x10004000) and the 16 records the image carries. */
#include "AdrenoFirmware.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const AdrenoFirmwareRegion regions[] = {
  {0, 0x4000, 1}, {0x4000, 0xffc000, 2}, {0x10004000, 0x4000, 3}
};

int main(int argc, char **argv)
{
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file && !fseek(file, 0, SEEK_END));
  long length = ftell(file);
  assert(length == 41548 && !fseek(file, 0, SEEK_SET));
  uint8_t *data = malloc((size_t)length);
  assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  AdrenoFirmwareBlock blocks[32];
  size_t count = 0;
  assert(AdrenoFirmwarePlanGmu(data, length, regions, 3, blocks, 32, &count) == AdrenoFirmwareSuccess && count == 16);
  unsigned payloads = 0;
  size_t payload_bytes = 0;
  for (size_t i = 0; i < count; ++i) {
    payloads += blocks[i].bytes != 0;
    payload_bytes += blocks[i].bytes;
    if (!blocks[i].bytes) assert(blocks[i].region_identifier == UINT32_MAX);
  }
  /* the zero-size records name the GMU's own layout (ITCM, DTCM, icache, dummy); the seven payloads carry the code */
  assert(payloads == 7 && payload_bytes == 41292);
  assert(blocks[8].address == 0x60000000 && !blocks[8].bytes && blocks[8].value == 0x1000);   /* the 4 KiB dummy */
  assert(blocks[9].address == 0 && blocks[9].bytes == 0xc0 && blocks[9].region_identifier == 1);
  assert(blocks[10].address == 0x200 && blocks[10].bytes == 0x140 && blocks[10].region_identifier == 1);
  assert(blocks[11].address == 0x400 && blocks[11].bytes == 0x370c && blocks[11].region_identifier == 1);
  assert(blocks[12].address == 0x4000 && blocks[12].bytes == 0x55d0 && blocks[12].region_identifier == 2);
  assert(blocks[13].address == 0x157000 && blocks[13].bytes == 0x398 && blocks[13].region_identifier == 2);
  assert(blocks[14].address == 0x10004000 && blocks[14].bytes == 0xec0 && blocks[14].region_identifier == 3);
  assert(blocks[15].address == 0x10007fdc && blocks[15].bytes == 0x18 && blocks[15].region_identifier == 3);
  unsigned valid_prefixes = 0;
  for (size_t bytes = 0; bytes < (size_t)length; ++bytes) {
    count = 0;
    if (AdrenoFirmwarePlanGmu(data, bytes, regions, 3, blocks, 32, &count) == AdrenoFirmwareSuccess) valid_prefixes++;
  }
  assert(valid_prefixes == 15);
  /* without the DTCM region its two payloads have nowhere to go; a short block table is refused before any output */
  count = 0;
  assert(AdrenoFirmwarePlanGmu(data, length, regions, 2, blocks, 32, &count) == AdrenoFirmwareUnmapped && !count);
  assert(AdrenoFirmwarePlanGmu(data, length, regions, 3, blocks, 15, &count) == AdrenoFirmwareNoSpace && !count);
  free(data);
  puts("PASS: actual 16-record a650 GMU plan, all 41548 prefixes and two refusal controls; no partial output");
  return 0;
}
