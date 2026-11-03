/* Exercise the production AUDEC driver with known software-front-end inputs. */
#include <board.h>
#include "drv_audec.h"
#include <stdint.h>
#include <string.h>
#include "mp3_vectors.h"

static uint32_t pcm[578] __attribute__((aligned(16)));
static uint32_t spectrum[AB32_AUDEC_SAMPLES], bands[AB32_AUDEC_BANDS];

static void dump(unsigned n)
{
    static const char hex[] = "0123456789abcdef";
    char line[65];
    const unsigned char *p = (const unsigned char *)&pcm[1];
    rt_kprintf("PCM %u 2304\n", n);
    for (unsigned i = 0; i < 2304; i += 32)
    {
        for (unsigned j = 0; j < 32; j++)
        {
            line[j * 2] = hex[p[i + j] >> 4];
            line[j * 2 + 1] = hex[p[i + j] & 15];
        }
        line[64] = 0;
        rt_kprintf("%s\n", line);
    }
}

static int mp3hw(void)
{
    rt_device_t dev = rt_device_find("audec0");
    if (!dev) return -RT_ENOSYS;
    rt_err_t rc = rt_device_control(dev, AB32_AUDEC_BEGIN, RT_NULL);
    if (rc) { rt_kprintf("AUDEC begin failed: %d\n", rc); return rc; }
    rt_kprintf("MP3HW begin driver=audec0\n");
    unsigned n;
    for (n = 0; n < MP3_VECTOR_COUNT; n++)
    {
        const struct mp3_vector *v = &mp3_vectors[n];
        memset(spectrum, 0, sizeof(spectrum));
        memset(bands, 0, sizeof(bands));
        for (unsigned i = v->first; i < v->first + v->count; i++)
        {
            unsigned offset = mp3_words[i].offset;
            if (offset >= 0x1000 && offset < 0x1900)
                spectrum[(offset - 0x1000) / 4] = mp3_words[i].value;
            else if (offset >= 0x2b00 && offset < 0x2b9c)
                bands[(offset - 0x2b00) / 4] = mp3_words[i].value;
            else { rc = -RT_EINVAL; break; }
        }
        if (rc) break;
        for (unsigned i = 0; i < 578; i++) pcm[i] = 0x5aa55aa5;
        struct ab32_audec_granule g = {
            .spectrum = spectrum, .bands = bands, .pcm = &pcm[1],
            .pcm_bytes = AB32_AUDEC_PCM_BYTES, .block_type = (v->control >> 12) & 3u,
        };
        rc = rt_device_control(dev, AB32_AUDEC_DECODE, &g);
        struct ab32_audec_status s;
        rt_err_t status_rc = rt_device_control(dev, AB32_AUDEC_GET_STATUS, &s);
        if (status_rc) { rc = status_rc; break; }
        unsigned changed = 0;
        for (unsigned i = 1; i <= 576; i++) changed += pcm[i] != 0x5aa55aa5;
        unsigned guards = pcm[0] == 0x5aa55aa5 && pcm[577] == 0x5aa55aa5;
        rt_kprintf("GRANULE %u status=%08x con=%08x ticks=%u changed=%u guards=%u\n",
                   n, s.last_status, s.last_control, s.last_ticks, changed, guards);
        if (rc || changed != 576 || !guards) { if (!rc) rc = -RT_EIO; break; }
        dump(n);
    }
    rt_err_t end_rc = rt_device_control(dev, AB32_AUDEC_END, RT_NULL);
    if (!rc) rc = end_rc;
    rt_kprintf("MP3HW end rc=%d granules=%u\n", rc, n);
    return rc;
}
MSH_CMD_EXPORT(mp3hw, Validate AUDEC driver with known MP3 inputs);
