/* Exercise the production AUDEC driver with known software-front-end inputs. */
#include <board.h>
#include "drv_audec.h"
#include <stdint.h>
#include <string.h>
#include "mp3_vectors.h"

/* Allocate only during this shell command, so the integrated image can run
 * BLE after the probe without retaining test-only sample buffers. */
struct probe_channel {uint32_t spectrum[AB32_AUDEC_SAMPLES], bands[AB32_AUDEC_BANDS];};
struct probe_buffers {uint32_t pcm[578]; struct probe_channel channel[];};

static void dump(unsigned n, const uint32_t *pcm)
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

static int mp3hw(int argc, char **argv)
{
    const struct mp3_case *test = &mp3_cases[0];
    if (argc > 2) return -RT_EINVAL;
    if (argc == 2)
    {
        unsigned i;
        for (i = 0; i < MP3_CASE_COUNT; i++) if (!strcmp(argv[1], mp3_cases[i].name)) break;
        if (i == MP3_CASE_COUNT) return -RT_EINVAL;
        test = &mp3_cases[i];
    }
    rt_device_t dev = rt_device_find("audec0");
    if (!dev) return -RT_ENOSYS;
    struct probe_buffers *buffers = rt_malloc(sizeof(*buffers) + test->channels * sizeof(struct probe_channel));
    if (!buffers) return -RT_ENOMEM;
    uint32_t *pcm = buffers->pcm;
    rt_err_t rc = rt_device_control(dev, AB32_AUDEC_BEGIN, RT_NULL);
    if (rc) { rt_kprintf("AUDEC begin failed: %d\n", rc); rt_free(buffers); return rc; }
    rt_kprintf("MP3HW begin driver=audec0 profile=%s\n", test->name);
    unsigned n;
    for (n = 0; n < test->count; n++)
    {
        const struct mp3_vector *v = &test->vectors[n];
        memset(buffers->channel, 0, test->channels * sizeof(struct probe_channel));
        for (unsigned i = v->first; i < v->first + v->count; i++)
        {
            unsigned offset = test->words[i].offset;
            uint32_t *target = RT_NULL;
            for (unsigned c = 0; c < test->channels; c++)
            {
                unsigned base = c ? 0x1d80 : 0x1000;
                unsigned band = 0x2b00 + c * 0x9c;
                if (offset >= base && offset < base + 0x900)
                    target = &buffers->channel[c].spectrum[(offset - base) / 4];
                else if (offset >= band && offset < band + 0x9c)
                    target = &buffers->channel[c].bands[(offset - band) / 4];
            }
            if (!target) { rc = -RT_EINVAL; break; }
            *target = test->words[i].value;
        }
        if (rc) break;
        for (unsigned i = 0; i < 578; i++) pcm[i] = 0x5aa55aa5;
        struct ab32_audec_granule g = {
            .spectrum = buffers->channel[0].spectrum, .bands = buffers->channel[0].bands, .pcm = &pcm[1],
            .pcm_bytes = AB32_AUDEC_PCM_BYTES, .block_type = (v->control >> 12) & 3u,
        };
        if (test->channels == 2)
        {
            unsigned mode = (v->control >> 8) & 3, extension = (v->control >> 10) & 3;
            if (mode == 0 && extension == 0) g.mode = AB32_AUDEC_STEREO;
            else if (mode == 1 && extension == 2) g.mode = AB32_AUDEC_MS_STEREO;
            else { rc = -RT_ENOSYS; break; }
            g.spectrum_right = buffers->channel[1].spectrum;
            g.bands_right = buffers->channel[1].bands;
            g.block_type_right = (v->control >> 15) & 3;
        }
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
        dump(n, pcm);
    }
    rt_err_t end_rc = rt_device_control(dev, AB32_AUDEC_END, RT_NULL);
    if (!rc) rc = end_rc;
    rt_kprintf("MP3HW end rc=%d granules=%u\n", rc, n);
    rt_free(buffers);
    return rc;
}
MSH_CMD_EXPORT(mp3hw, Validate AUDEC driver with known MP3 inputs);
