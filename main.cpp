#include <iostream>
#include <vector>
#include <cmath>
#include <x86intrin.h>    //AVX/SSE Extensions

#include "VCL/vectorclass.h"

using namespace std;

const int NUM_IT = 500;
const int S      = 1024;
constexpr int XY = S*S;
constexpr int N  = XY/8;

inline int BITSELECT(int condition, int truereturnvalue, int falsereturnvalue){
    return (truereturnvalue & -condition) | (falsereturnvalue & ~(-condition)); 
}
inline float BITSELECT(int condition, float truereturnvalue, float falsereturnvalue) {
    int& at = reinterpret_cast<int&>(truereturnvalue);
    int& af = reinterpret_cast<int&>(falsereturnvalue);
    int res = (at & -condition) | (af & ~(-condition)); //a when TRUE and b when FALSE
    return  reinterpret_cast<float&>(res);
}

int kernel1(float ax, float ay){
    float x = 0.f; float y = 0.f;
    int n = 0;
    for(n = 0; n < NUM_IT ; ++n){
        float newx = x*x - y*y + ax;
        float newy = 2.f*x*y + ay;
        if(4.f < newx*newx + newy*newy) return n;
        x = newx; y = newy;
    }
    return NUM_IT;
}
int kernel2(float ax, float ay){
    float x = 0.f; float y = 0.f;
    int n = 0;
    for(int i = 0; i < NUM_IT; ++i) {
        const float newx = x*x - y*y + ax;
        const float newy = 2.f*x*y + ay;
        const int mask = 1 - ((int) (4.f < newx*newx + newy*newy)); 
        n += BITSELECT(mask, 1, 0);
        x =  BITSELECT(mask, newx, x);
        y =  BITSELECT(mask, newy, y);
    }
    return n;
}
template<class Kernel>
void mandelbrot_aos(std::vector<float>& arr, size_t X, size_t Y, Kernel kernel){
    const size_t XY = X*Y;
    for(size_t xy = 0; xy < XY; ++xy) {
        const float ax = ((float) (xy % X) / (float) X) / 200.f - 0.7463f;
        const float ay = ((float) (xy / X) / (float) Y) / 200.f + 0.1102f;
        arr[xy]        = (float) kernel(ax, ay);
    }
}

inline __m256 kernel(__m256 ax, __m256 ay)  {
    __m256 mone = _mm256_set1_ps(-1.0f);
    __m256 one  = _mm256_set1_ps(1.0f);
    __m256 two  = _mm256_set1_ps(2.0f);
    __m256 four = _mm256_set1_ps(4.0f);
    __m256 res  = _mm256_set1_ps(0.0f);
    __m256 x    = _mm256_set1_ps(0.0f);
    __m256 y    = _mm256_set1_ps(0.0f);

    for (int n = 0; n < NUM_IT; n++) {
        __m256 newx = _mm256_add_ps(_mm256_sub_ps(_mm256_mul_ps(x, x), _mm256_mul_ps(y, y)), ax);
        __m256 newy = _mm256_add_ps(_mm256_mul_ps(two, _mm256_mul_ps(x, y)), ay);
        __m256 norm = _mm256_add_ps(_mm256_mul_ps(newx, newx), _mm256_mul_ps(newy, newy));
        __m256 cmpmask = _mm256_cmp_ps(four, norm, _CMP_LT_OS);
        res = _mm256_blendv_ps(_mm256_add_ps(res, one), res, cmpmask);

        x = _mm256_blendv_ps(newx, x, cmpmask);
        y = _mm256_blendv_ps(newy, y, cmpmask);

        if(_mm256_testc_ps(cmpmask, mone) ){
            return res;
        }

    }
    return res;
}

float* vals = (float*) aligned_alloc(32, 8);
void mandelbrot_aos_intr(std::vector<float>& arr, size_t X, size_t Y){
    const size_t XY = X*Y;
    __m256 ax = _mm256_set1_ps(-0.7463f);
    __m256 ay = _mm256_set1_ps( 0.1102f);
    for(size_t xy = 0; xy < XY; xy +=8) {
        for(int i = 0; i < 8; ++i) {
            ax[i] = ((float) ((xy+i) % X) / (float) X) / 200.f -0.7463f;
            ay[i] = ((float) ((xy+i) / X) / (float) Y) / 200.f +0.1102f ;
        }
        __m256 res = kernel(ax, ay);
        _mm256_store_ps(vals,  res);

        std::copy(vals, vals+8, arr.begin() + xy);
    }

}

void mandelbrot_soa(std::vector<float>& arr, size_t X, size_t Y){ 
    const size_t XY = X*Y;
    std::vector<float> xs(XY, 0.f), ys(XY, 0.f), axs(XY, 0.f), ays(XY, 0.f);
    for(size_t xy = 0; xy < XY; ++xy) {
        axs[xy] = ((float) (xy % X) / (float) X) / 200.f - 0.7463f;
        ays[xy] = ((float) (xy / X) / (float) Y) / 200.f + 0.1102f;
    }

    for(size_t i = 0; i < NUM_IT; ++i) {
        #pragma GCC ivdep
        for(size_t xy = 0; xy < XY; ++xy) {
          const float newx = xs[xy] * xs[xy] - ys[xy] *ys[xy] + axs[xy];
          const float newy = 2.f * xs[xy]*ys[xy] + ays[xy];
          const int   mask = 1 - ((int) (4.f < newx*newx + newy*newy));
          arr[xy] += BITSELECT(mask, 1.f, 0.f);
           xs[xy]  = BITSELECT(mask, newx, xs[xy]);
           ys[xy]  = BITSELECT(mask, newy, ys[xy]);
        }
    }
}

Vec8f kernel_vcl(Vec8f ax, Vec8f ay){
    Vec8f x = 0.f, y = 0.f, count = 0.f;
    for(int n = 0; n < NUM_IT ; ++n){
        Vec8f newx = x*x - y*y + ax;
        Vec8f newy = 2.f * x*y + ay;
        Vec8fb mask= 4.f < newx*newx + newy*newy;
        count = select(mask, count, count + 1);
        x     = select(mask,     x,         newx);
        y     = select(mask,     y,         newy);
        if ( horizontal_and(mask) ) {
            return count;
        }
    }
    return count;
}
void mandelbrot_VCL(std::vector<float>& arr, size_t X, size_t Y){
    Vec8f ax8, ay8;
    for(int xy = 0; xy < XY; xy += 8) {
        for(int i = 0; i < 8; ++i) {
            ax8.insert(i, ((float) ((xy+i) % X) / (float) X ) / 200.f - 0.7463f);
            ay8.insert(i, ((float) ((xy+i) / X) / (float) Y ) / 200.f + 0.1102);
        }
        Vec8f count = kernel_vcl(ax8, ay8);
        count.store(arr.data() + xy);
    }
}

#include <chrono>
using namespace chrono;
auto t1 = high_resolution_clock::now();
auto t2 = high_resolution_clock::now();

int main(int argc, char** argv){

    std::vector<float> arr1(XY), arr2(XY), arr3(XY), arr4(XY);

    t1 = high_resolution_clock::now();
    mandelbrot_aos_intr(arr1, S, S);
    t2 = high_resolution_clock::now();
    auto t_intr = duration_cast<duration<double>>(t2 - t1).count();

    t1 = high_resolution_clock::now();
    mandelbrot_aos(arr2, S, S, [] (float ax, float ay) {return kernel1(ax,ay);});
    t2 = high_resolution_clock::now();
    auto t_naiv = duration_cast<duration<double>>(t2 - t1).count();

    t1 = high_resolution_clock::now();
    mandelbrot_soa(arr3, S, S);
    t2 = high_resolution_clock::now();
    auto t_soa = duration_cast<duration<double>>(t2 - t1).count();

    t1 = high_resolution_clock::now();
    mandelbrot_VCL(arr4, S, S);
    t2 = high_resolution_clock::now();
    auto t_vcl = duration_cast<duration<double>>(t2 - t1).count();

    cout << "(1) Time intrinsic: " << t_intr << endl;
    cout << "(2) Time naive    : " << t_naiv << endl;
    cout << "(3) Time autovec  : " << t_soa << endl;
    cout << "(4) Time VCL      : " << t_vcl << endl;
    cout << "Improvement(1): " << (t_naiv / t_intr) << " X" << endl;
    cout << "Improvement(2): " << (t_naiv / t_vcl) << " X" << endl;

    bool correct = true;
    double MSE = 0.0;
    for(int i = 0; i < S; ++i){
        for(int j = 0; j < S; ++j){
            MSE += std::pow((arr2[i+S*j] - arr1[i+S*j])/NUM_IT, 2);
            MSE += std::pow((arr3[i+S*j] - arr1[i+S*j])/NUM_IT, 2);
            MSE += std::pow((arr4[i+S*j] - arr1[i+S*j])/NUM_IT, 2);
        }
    }

    cout << "MSE "<< (MSE / (3*XY)) <<endl;
    return 0;
}
