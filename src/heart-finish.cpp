// heart_finish: the read-aloud post-processing for heart (gain, noise gate, consonant lift), in C++.
// Same steps and constants as read-buffer-aloud.py heart_finish(). Reads raw int16 mono 24 kHz on stdin,
// writes int16 on stdout. usage: heart_finish <gain> <gate_db|none> <consonant_db|none>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <vector>


struct cd { double re, im; cd(double r = 0, double i = 0) : re(r), im(i) {} };
static inline cd operator+(cd a, cd b) { return cd(a.re + b.re, a.im + b.im); }
static inline cd operator*(cd a, cd b) { return cd(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re); }
static inline cd& operator+=(cd& a, cd b) { a.re += b.re; a.im += b.im; return a; }
static std::vector<cd> TW;   // exp(-2*pi*i*j/240)
// mixed-radix decimation-in-time FFT for any n dividing HOP (240 = 2^4 * 3 * 5)
static void fft_rec(const cd * in, cd * out, int n, int stride) {
    if (n == 1) { out[0] = in[0]; return; }
    int p = 2; while (n % p) ++p;
    const int m = n / p;
    for (int r = 0; r < p; ++r) fft_rec(in + r * stride, out + r * m, m, stride * p);
    std::vector<cd> t((size_t)n);
    const int step = 240 / n;
    for (int k = 0; k < m; ++k)
        for (int q = 0; q < p; ++q) {
            const int idx = k + q * m;
            cd sum = 0;
            for (int r = 0; r < p; ++r) sum += out[r * m + k] * TW[(size_t)((r * idx) % n) * step];
            t[(size_t)idx] = sum;
        }
    for (int i = 0; i < n; ++i) out[i] = t[(size_t)i];
}

static const int RATE = 24000, HOP = 240;              // 10 ms frames
static const double HF_HZ = 3500.0, HF_SHARE = 0.30;
static const int FADE = 120;                           // 5 ms
static const int SMOOTH = 360;                         // 15 ms (lift_consonants smooth=0.015)

// centered, zero-padded moving average, same as numpy smooth_envelope(): pads (w/2, (w-1)/2)
static std::vector<float> smooth(const std::vector<float> & x, int width) {
    const long n = (long)x.size(), before = width / 2;
    std::vector<double> cs((size_t)n + width + 1, 0.0);
    for (long i = 0; i < n + width - 1; ++i) {
        long j = i - before;
        cs[(size_t)i + 1] = cs[(size_t)i] + ((j >= 0 && j < n) ? (double)x[(size_t)j] : 0.0);
    }
    std::vector<float> out((size_t)n);
    for (long i = 0; i < n; ++i) out[(size_t)i] = (float)((cs[(size_t)i + width] - cs[(size_t)i]) / width);
    return out;
}
// centered window sums of a 0/1 vector (box_sum), always input length
static std::vector<double> box_sum(const std::vector<double> & x, int width) {
    const long n = (long)x.size(), before = width / 2;
    std::vector<double> cs((size_t)n + width + 1, 0.0);
    for (long i = 0; i < n + width - 1; ++i) {
        long j = i - before;
        cs[(size_t)i + 1] = cs[(size_t)i] + ((j >= 0 && j < n) ? x[(size_t)j] : 0.0);
    }
    std::vector<double> out((size_t)n);
    for (long i = 0; i < n; ++i) out[(size_t)i] = cs[(size_t)i + width] - cs[(size_t)i];
    return out;
}
static std::vector<double> frame_rms(const std::vector<float> & s, size_t n) {
    std::vector<double> r(n);
    for (size_t k = 0; k < n; ++k) {
        double a = 0;
        for (int i = 0; i < HOP; ++i) { double v = s[k * HOP + i]; a += v * v; }
        r[k] = std::sqrt(a / HOP);
    }
    return r;
}
// speech_rms(): RMS over samples louder than 3% of the peak (all samples if none)
static double speech_rms(const std::vector<float> & s) {
    double mx = 0; for (float v : s) mx = std::max(mx, (double)std::fabs(v));
    const double thr = 0.03 * std::max(mx, 1.0);
    double acc = 0; size_t n = 0;
    for (float v : s) if (std::fabs(v) > thr) { acc += (double)v * v; ++n; }
    if (!n) { for (float v : s) acc += (double)v * v; n = s.size(); }
    return std::sqrt(acc / (double)n);
}
int main(int argc, char ** argv) {
    if (argc != 4) { fprintf(stderr, "usage: heart_finish <gain> <gate_db|none> <consonant_db|none>\n"); return 2; }
    const double gain_cfg = std::atof(argv[1]);
    const bool do_gate = std::strcmp(argv[2], "none") != 0, do_lift = std::strcmp(argv[3], "none") != 0;
    const double gate_db = do_gate ? std::atof(argv[2]) : 0, cons_db = do_lift ? std::atof(argv[3]) : 0;

    std::vector<int16_t> raw; { int16_t buf[4096]; size_t got; while ((got = fread(buf, 2, 4096, stdin)) > 0) raw.insert(raw.end(), buf, buf + got); }
    if (raw.empty()) { fprintf(stderr, "heart produced no audio\n"); return 1; }
    std::vector<float> s(raw.size());
    double mx = 0; for (size_t i = 0; i < raw.size(); ++i) { s[i] = raw[i]; mx = std::max(mx, (double)std::fabs(s[i])); }
    const float gain = (float)std::min(gain_cfg, 32000.0 / std::max(mx, 1.0));
    for (float & v : s) v *= gain;
    const size_t n = s.size() / HOP;

    if (do_gate && n) {                                  // gate_noise(): quiet 10 ms frames faded to silence
        auto rms = frame_rms(s, n);
        const double top = *std::max_element(rms.begin(), rms.end()) * std::pow(10.0, gate_db / 20);
        std::vector<double> keep(n); for (size_t k = 0; k < n; ++k) keep[k] = rms[k] > top ? 1.0 : 0.0;
        const int h = (int)std::lround(0.06 / 0.01);
        auto win = box_sum(keep, 2 * h + 1);
        std::vector<float> env(s.size());
        for (size_t i = 0; i < s.size(); ++i) env[i] = (i / HOP < n) ? (win[i / HOP] > 0 ? 1.0f : 0.0f) : (n ? (win[n - 1] > 0 ? 1.0f : 0.0f) : 0.0f);
        env = smooth(env, FADE);
        for (size_t i = 0; i < s.size(); ++i) s[i] *= env[i];
    }

    if (do_lift && n) {                                  // lift_consonants()
        auto rms = frame_rms(s, n);
        const double loud = *std::max_element(rms.begin(), rms.end()) * std::pow(10.0, -40.0 / 20);
        // frame spectra: 240-point real DFT with a Hann window (numpy hanning(HOP)); bins k*100 Hz
        static std::vector<double> han(HOP);
        static bool init = false;
        if (!init) {
            init = true;
            for (int i = 0; i < HOP; ++i) han[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (HOP - 1));
            TW.resize(HOP);
            for (int j = 0; j < HOP; ++j) TW[j] = cd(std::cos(2.0 * M_PI * j / HOP), -std::sin(2.0 * M_PI * j / HOP));
        }
        const int k0 = (int)std::ceil(HF_HZ / ((double)RATE / HOP) - 1e-9);
        std::vector<double> boost(n, 1.0);
        const double cons_gain = std::pow(10.0, cons_db / 20);
        // power spectrum per frame: 240 = 16 x 15 two-step DFT with precomputed tables (no allocation)
        static double W16r[16][16], W16i[16][16], W15r[15][15], W15i[15][15], TWr[15][16], TWi[15][16];
        static bool tables = false;
        if (!tables) {
            tables = true;
            for (int a = 0; a < 16; ++a) for (int b = 0; b < 16; ++b) {
                W16r[a][b] = std::cos(2.0 * M_PI * ((a * b) % 16) / 16); W16i[a][b] = -std::sin(2.0 * M_PI * ((a * b) % 16) / 16);
            }
            for (int a = 0; a < 15; ++a) for (int b = 0; b < 15; ++b) {
                W15r[a][b] = std::cos(2.0 * M_PI * ((a * b) % 15) / 15); W15i[a][b] = -std::sin(2.0 * M_PI * ((a * b) % 15) / 15);
            }
            for (int n2 = 0; n2 < 15; ++n2) for (int k1 = 0; k1 < 16; ++k1) {
                TWr[n2][k1] = std::cos(2.0 * M_PI * n2 * k1 / 240); TWi[n2][k1] = -std::sin(2.0 * M_PI * n2 * k1 / 240);
            }
        }
        double xw[240];
        double Ar[15][16], Ai[15][16];
        double pw_bin[121];
        for (size_t f = 0; f < n; ++f) {
            if (!(rms[f] > loud)) continue;      // quiet frames can never be boosted: skip their spectrum
            for (int i = 0; i < HOP; ++i) xw[i] = (double)(s[f * HOP + i] * han[i]);
            // step 1: for each n2, 16-point DFT over n1 of x[15*n1 + n2]; then twiddle by W^(n2*k1)
            for (int n2 = 0; n2 < 15; ++n2) for (int k1 = 0; k1 < 16; ++k1) {
                double re = 0, im = 0;
                for (int n1 = 0; n1 < 16; ++n1) {
                    const double v = xw[15 * n1 + n2];
                    re += v * W16r[k1][n1]; im += v * W16i[k1][n1];
                }
                Ar[n2][k1] = re * TWr[n2][k1] - im * TWi[n2][k1];
                Ai[n2][k1] = re * TWi[n2][k1] + im * TWr[n2][k1];
            }
            // step 2: for each k1, 15-point DFT over n2 gives bins k = k1 + 16*k2 (only k <= 120 needed)
            for (int k1 = 0; k1 < 16; ++k1) for (int k2 = 0; k2 < 15; ++k2) {
                const int k = k1 + 16 * k2;
                if (k > HOP / 2) continue;
                double re = 0, im = 0;
                for (int n2 = 0; n2 < 15; ++n2) {
                    re += Ar[n2][k1] * W15r[k2][n2] - Ai[n2][k1] * W15i[k2][n2];
                    im += Ar[n2][k1] * W15i[k2][n2] + Ai[n2][k1] * W15r[k2][n2];
                }
                pw_bin[k] = re * re + im * im;
            }
            double all = 0, hf = 0;
            for (int k = 0; k <= HOP / 2; ++k) {
                all += pw_bin[k]; if (k >= k0) hf += pw_bin[k];
            }
            const double share = hf / std::max(all, 1e-9);
            if (share >= HF_SHARE && rms[f] > loud) boost[f] = cons_gain;
        }
        std::vector<float> env(s.size());
        for (size_t i = 0; i < s.size(); ++i) env[i] = (float)((i / HOP < n) ? boost[i / HOP] : boost[n - 1]);
        env = smooth(env, SMOOTH);
        std::vector<float> lifted(s.size());
        for (size_t i = 0; i < s.size(); ++i) lifted[i] = s[i] * env[i];
        const double ratio = speech_rms(s) / std::max(speech_rms(lifted), 1.0);
        for (float & v : lifted) v *= (float)ratio;
        double peak = 0; for (float v : lifted) peak = std::max(peak, (double)std::fabs(v));
        if (peak > 32000) for (float & v : lifted) v *= (float)(32000 / peak);
        s.swap(lifted);
    }

    std::vector<int16_t> out(s.size());
    for (size_t i = 0; i < s.size(); ++i) out[i] = (int16_t)std::trunc(std::max(-32768.0f, std::min(32767.0f, s[i])));
    fwrite(out.data(), 2, out.size(), stdout);
    return 0;
}
