/**
    lim.ltc~ - SMPTE LTC (Linear Timecode) generator for Max/MSP.
    Copyright (c) 2026 Richard Moores
    Licensed under the MIT License

    Outputs LTC as an audio signal (Manchester/biphase encoded) using libltc.
    Control messages (left inlet):
        time H M S F   - set the timecode origin (hours mins secs frames)
        framerate F    - 24, 25, 29.97 (drop-frame), or 30
        start | 1      - begin output at the set time
        stop  | 0      - stop output (silence)

    libltc is statically linked (LGPLv3). See third_party/libltc/COPYING.
*/

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include "ltc.h"

#include <string.h>
#include <math.h>

typedef struct _ltc {
    t_pxobject           ob;

    LTCEncoder          *enc;       // libltc encoder (owns its sample buffer)
    double               sr;        // sample rate the encoder was built for
    double               fps;       // desired frames per second
    enum LTC_TV_STANDARD std;       // desired TV standard

    SMPTETimecode        tc;        // user-set origin timecode

    ltcsnd_sample_t     *cur_buf;   // -> encoder's current-frame buffer
    int                  cur_len;   // valid samples in cur_buf
    int                  cur_pos;   // read cursor into cur_buf

    long                 running;   // 1 = generating, 0 = silence
    long                 reconfig;  // fps/std changed; apply at next frame
    long                 tc_reload; // timecode changed; apply at next frame
} t_ltc;

static t_class *s_ltc_class = NULL;

void   *ltc_new(t_symbol *s, long argc, t_atom *argv);
void    ltc_free(t_ltc *x);
void    ltc_assist(t_ltc *x, void *b, long m, long a, char *s);
void    ltc_dsp64(t_ltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void    ltc_perform64(t_ltc *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);
void    ltc_time(t_ltc *x, t_symbol *s, long argc, t_atom *argv);
void    ltc_framerate(t_ltc *x, double f);
void    ltc_start(t_ltc *x);
void    ltc_stop(t_ltc *x);
void    ltc_pause(t_ltc *x);
void    ltc_int(t_ltc *x, long n);

static double ltc_normalize_fps(t_ltc *x, double fps_in, enum LTC_TV_STANDARD *std_out);
static void   ltc_configure_encoder(t_ltc *x, double samplerate);

void ext_main(void *r)
{
    t_class *c = class_new("lim.ltc~",
                           (method)ltc_new,
                           (method)ltc_free,
                           sizeof(t_ltc), 0L, A_GIMME, 0);

    class_addmethod(c, (method)ltc_dsp64,     "dsp64",     A_CANT,  0);
    class_addmethod(c, (method)ltc_assist,    "assist",    A_CANT,  0);
    class_addmethod(c, (method)ltc_time,      "time",      A_GIMME, 0);
    class_addmethod(c, (method)ltc_framerate, "framerate", A_FLOAT, 0);
    class_addmethod(c, (method)ltc_start,     "start",     0);
    class_addmethod(c, (method)ltc_stop,      "stop",      0);
    class_addmethod(c, (method)ltc_pause,     "pause",     0);
    class_addmethod(c, (method)ltc_int,       "int",       A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_ltc_class = c;
}

void *ltc_new(t_symbol *s, long argc, t_atom *argv)
{
    t_ltc *x = (t_ltc *)object_alloc(s_ltc_class);
    if (!x)
        return NULL;

    dsp_setup((t_pxobject *)x, 1);        // 1 inlet (signal ignored; used for messages)
    outlet_new((t_object *)x, "signal");  // LTC audio out

    x->enc = NULL;
    x->sr  = sys_getsr() > 0 ? sys_getsr() : 48000.0;
    x->fps = 30.0;
    x->std = LTC_TV_525_60;

    memset(&x->tc, 0, sizeof(x->tc));
    strncpy(x->tc.timezone, "+0000", sizeof(x->tc.timezone));
    x->tc.months = 1;
    x->tc.days   = 1;

    x->cur_buf  = NULL;
    x->cur_len  = 0;
    x->cur_pos  = 0;
    x->running  = 0;
    x->reconfig = 0;
    x->tc_reload = 0;

    // Optional first argument: initial frame rate.
    if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
        enum LTC_TV_STANDARD std;
        x->fps = ltc_normalize_fps(x, atom_getfloat(argv), &std);
        x->std = std;
    }

    return x;
}

void ltc_free(t_ltc *x)
{
    dsp_free((t_pxobject *)x);
    if (x->enc)
        ltc_encoder_free(x->enc);
}

void ltc_assist(t_ltc *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET)
        sprintf(s, "messages: time H M S F / framerate F / start / stop / 1|0 (signal in ignored)");
    else
        sprintf(s, "(signal) LTC timecode audio");
}

// Map a requested frame rate to libltc's fps + TV standard.
static double ltc_normalize_fps(t_ltc *x, double fps_in, enum LTC_TV_STANDARD *std_out)
{
    int code = (int)floor(fps_in * 100.0 + 0.5);   // e.g. 29.97 -> 2997
    double fps;
    enum LTC_TV_STANDARD std;

    switch (code) {
        case 2400: fps = 24.0;             std = LTC_TV_FILM_24; break;
        case 2397: fps = 24000.0 / 1001.0; std = LTC_TV_FILM_24; break; // 23.976
        case 2500: fps = 25.0;             std = LTC_TV_625_50;  break;
        case 2997: fps = 30000.0 / 1001.0; std = LTC_TV_525_60;  break; // 29.97 drop-frame
        case 3000: fps = 30.0;             std = LTC_TV_525_60;  break;
        default:
            object_warn((t_object *)x, "unsupported frame rate %g; using 30", fps_in);
            fps = 30.0; std = LTC_TV_525_60; break;
    }
    if (std_out)
        *std_out = std;
    return fps;
}

// (Re)build the encoder for the given sample rate. Runs off the audio thread.
static void ltc_configure_encoder(t_ltc *x, double samplerate)
{
    x->sr = samplerate;

    if (x->enc) {
        ltc_encoder_free(x->enc);
        x->enc = NULL;
    }

    // Allocate the sample buffer for the lowest supported fps (~23.976), which
    // is the largest a frame can be. Subsequent ltc_encoder_reinit() calls then
    // stay within this size and never reallocate (safe on the audio thread).
    x->enc = ltc_encoder_create(samplerate, 24000.0 / 1001.0, LTC_TV_FILM_24, 0);
    if (!x->enc) {
        object_error((t_object *)x, "could not allocate LTC encoder");
        return;
    }

    ltc_encoder_reinit(x->enc, samplerate, x->fps, x->std, 0);
    ltc_encoder_set_timecode(x->enc, &x->tc);

    x->cur_buf  = NULL;
    x->cur_len  = 0;
    x->cur_pos  = 0;
    x->reconfig = 0;
    x->tc_reload = 0;
}

void ltc_dsp64(t_ltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (!x->enc || x->sr != samplerate)
        ltc_configure_encoder(x, samplerate);

    object_method(dsp64, gensym("dsp_add64"), x, ltc_perform64, 0, NULL);
}

void ltc_perform64(t_ltc *x, t_object *dsp64, double **ins, long numins,
                   double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    double *out = outs[0];
    long    n   = sampleframes;

    if (!x->running || !x->enc) {
        while (n-- > 0)
            *out++ = 0.0;
        return;
    }

    while (n > 0) {
        if (x->cur_pos >= x->cur_len) {
            // Apply staged changes at the frame boundary (no allocation).
            if (x->reconfig) {
                ltc_encoder_reinit(x->enc, x->sr, x->fps, x->std, 0); // keeps running time
                x->reconfig = 0;
            }
            if (x->tc_reload) {
                ltc_encoder_set_timecode(x->enc, &x->tc);
                x->tc_reload = 0;
            }

            ltc_encoder_encode_frame(x->enc);
            x->cur_len = ltc_encoder_get_bufferptr(x->enc, &x->cur_buf, 1);
            x->cur_pos = 0;
            ltc_encoder_inc_timecode(x->enc);

            if (x->cur_len <= 0 || x->cur_buf == NULL) {
                while (n-- > 0)
                    *out++ = 0.0;
                return;
            }
        }

        while (n > 0 && x->cur_pos < x->cur_len) {
            // 8-bit unsigned (center 128) -> float [-1, 1)
            *out++ = ((double)x->cur_buf[x->cur_pos++] - 128.0) / 128.0;
            --n;
        }
    }
}

void ltc_time(t_ltc *x, t_symbol *s, long argc, t_atom *argv)
{
    long h   = argc > 0 ? atom_getlong(argv + 0) : 0;
    long m   = argc > 1 ? atom_getlong(argv + 1) : 0;
    long sec = argc > 2 ? atom_getlong(argv + 2) : 0;
    long f   = argc > 3 ? atom_getlong(argv + 3) : 0;
    long maxf;

    if (h < 0) h = 0; else if (h > 23) h = 23;
    if (m < 0) m = 0; else if (m > 59) m = 59;
    if (sec < 0) sec = 0; else if (sec > 59) sec = 59;
    maxf = (long)floor(x->fps + 0.5) - 1;
    if (maxf < 0) maxf = 0;
    if (f < 0) f = 0; else if (f > maxf) f = maxf;

    x->tc.hours = (unsigned char)h;
    x->tc.mins  = (unsigned char)m;
    x->tc.secs  = (unsigned char)sec;
    x->tc.frame = (unsigned char)f;

    x->tc_reload = 1;
    if (x->enc && !x->running) {
        // Safe to touch the encoder directly while stopped.
        ltc_encoder_set_timecode(x->enc, &x->tc);
        x->cur_pos   = x->cur_len; // next start begins on a fresh frame
        x->tc_reload = 0;
    }
}

void ltc_framerate(t_ltc *x, double f)
{
    enum LTC_TV_STANDARD std;
    double fps = ltc_normalize_fps(x, f, &std);

    x->fps = fps;
    x->std = std;
    x->reconfig = 1;

    if (x->enc && !x->running) {
        ltc_encoder_reinit(x->enc, x->sr, x->fps, x->std, 0);
        ltc_encoder_set_timecode(x->enc, &x->tc);
        x->cur_pos  = x->cur_len;
        x->reconfig = 0;
    }
}

void ltc_start(t_ltc *x)
{
    x->tc_reload = 1;            // begin at the set time
    x->cur_pos   = x->cur_len;   // force a fresh frame
    x->running   = 1;
}

void ltc_stop(t_ltc *x)
{
    x->running = 0;
}

void ltc_pause(t_ltc *x)
{
    x->running = !x->running;  // pause/resume in place (keeps running timecode)
}

void ltc_int(t_ltc *x, long n)
{
    if (n)
        ltc_start(x);
    else
        ltc_stop(x);
}
