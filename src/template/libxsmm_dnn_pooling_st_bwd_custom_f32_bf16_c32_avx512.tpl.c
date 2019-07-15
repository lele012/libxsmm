/******************************************************************************
** Copyright (c) 2017-2019, Intel Corporation                                **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Alexander Heinecke, Sasikanth Avancha (Intel Corp.)
******************************************************************************/

#if defined(LIBXSMM_DNN_POOLING_BWD_BF16)
# define _mm512_load_act(A)     _mm512_castsi512_ps(_mm512_slli_epi32(_mm512_cvtepi16_epi32(_mm256_loadu_si256((__m256i*)(A))),16))
#if 1
# define _mm512_roundbf16rne(A) LIBXSMM_INTRINSICS_MM512_ROUNDNE_BF16(A)
# define _mm512_stream_act(A,B) _mm256_stream_si256((__m256i*)(A),_mm512_cvtepi32_epi16(_mm512_srai_epi32(_mm512_roundbf16rne((B)),16)))
# define _mm512_store_act(A,B)  _mm256_storeu_si256((__m256i*)(A),_mm512_cvtepi32_epi16(_mm512_srai_epi32(_mm512_roundbf16rne((B)),16)))
#else
# define _mm512_stream_act(A,B) _mm256_stream_si256((__m256i*)(A),_mm512_cvtepi32_epi16(_mm512_srai_epi32(_mm512_castps_si512((B)),16)))
# define _mm512_store_act(A,B)  _mm256_storeu_si256((__m256i*)(A),_mm512_cvtepi32_epi16(_mm512_srai_epi32(_mm512_castps_si512((B)),16)))
#endif
#else
# define _mm512_load_act(A)     _mm512_loadu_ps(A)
# define _mm512_stream_act(A,B) _mm512_stream_ps(A,B)
# define _mm512_store_act(A,B)  _mm512_storeu_ps(A,B)
#endif

/* size variables, all const */
const int nImg = handle->desc.N;
const int ifh = handle->desc.H;
const int ifw = handle->desc.W;
#if defined(LIBXSMM_DNN_POOLING_BWD_AVG)
const int sh = handle->desc.u;
const int sw = handle->desc.v;
#endif
const int ofh = handle->ofh;
const int ofw = handle->ofw;
const int iph = handle->desc.pad_h_in;
const int ipw = handle->desc.pad_w_in;
const int oph = handle->desc.pad_h_out;
const int opw = handle->desc.pad_w_out;
const int ofhp = ofh + 2*oph;
const int ofwp = ofw + 2*opw;
const int ifhp = ifh + 2*iph;
const int ifwp = ifw + 2*ipw;
/* here we assume that input and output blocking is similar */
const int nBlocksFm = handle->blocksifm;

/* computing first logical thread */
const int ltid = tid - start_thread;
/* number of tasks that could be run in parallel */
const int work = nImg * nBlocksFm;
/* compute chunk size */
const int chunksize = (work % handle->desc.threads == 0) ? (work / handle->desc.threads) : ((work / handle->desc.threads) + 1);
/* compute thr_begin and thr_end */
const int thr_begin = (ltid * chunksize < work) ? (ltid * chunksize) : work;
const int thr_end = ((ltid + 1) * chunksize < work) ? ((ltid + 1) * chunksize) : work;

/* loop variables */
int img = 0;
int fm = 0;
int imgfm = 0;
int ho = 0;
int wo = 0;
int hi = 0;
int wi = 0;
int v = 0;
#if defined(LIBXSMM_DNN_POOLING_BWD_AVG)
int kh = 0;
int kw = 0;
#if defined(LIBXSMM_DNN_POOLING_BWD_BF16)
float recp_pool_size = 1.0f/((float)handle->desc.R*(float)handle->desc.S);
#else
element_input_type recp_pool_size = 1.0f/((element_input_type)handle->desc.R*(element_input_type)handle->desc.S);
#endif
#endif

/* multi-dim arrays declaration */
#if defined(LIBXSMM_DNN_POOLING_BWD_BF16)
float* lcl_buffer_ptr = ((float*)handle->scratch)+((size_t)ifh*(size_t)ifw*(size_t)32*(size_t)ltid);
LIBXSMM_VLA_DECL(3,       float, lcl_dinput, lcl_buffer_ptr,                                                    ifw, 32);
#else
element_output_type* lcl_buffer_ptr = ((element_input_type*)handle->scratch)+((size_t)ifh*(size_t)ifw*(size_t)32*(size_t)ltid);
LIBXSMM_VLA_DECL(3,       element_input_type, lcl_dinput, lcl_buffer_ptr,                                                    ifw, 32);
#endif
LIBXSMM_VLA_DECL(5,       element_input_type,     dinput, (element_input_type* )handle->grad_input->data,  nBlocksFm, ifhp, ifwp, 32);
LIBXSMM_VLA_DECL(5, const element_output_type,   doutput, (element_output_type*)handle->grad_output->data, nBlocksFm, ofhp, ofwp, 32);
#if defined(LIBXSMM_DNN_POOLING_BWD_MAX)
LIBXSMM_VLA_DECL(5, const  element_mask_type,        mask, (element_mask_type* )handle->mask->data,        nBlocksFm,  ofh,  ofw, 32);
#endif

/* lazy barrier init */
libxsmm_barrier_init(handle->barrier, ltid);

for (imgfm = thr_begin; imgfm < thr_end; ++imgfm) {
  img = imgfm / nBlocksFm;
  fm = imgfm % nBlocksFm;

  for( v = 0; v < ifh*ifw*32; v += 16 ) {
    _mm512_storeu_ps( &(lcl_buffer_ptr[v]), _mm512_setzero_ps() );
  }

#if defined(LIBXSMM_DNN_POOLING_BWD_MAX)
  for( ho = oph; ho < (ofh+oph); ho++ ) {
    for( wo = opw; wo < (ofw+opw); wo++ ) {
      __m512 lcl_vdinput, lcl_vdinput2;
      const element_output_type* doutput_ptr = &LIBXSMM_VLA_ACCESS(5, doutput, img, fm,     ho,     wo, 0, nBlocksFm, ofhp, ofwp, 32);
      const element_mask_type*      mask_ptr = &LIBXSMM_VLA_ACCESS(5, mask,    img, fm, ho-oph, wo-opw, 0, nBlocksFm,  ofh,  ofw, 32);

      lcl_vdinput = _mm512_i32gather_ps( _mm512_loadu_si512( mask_ptr ), lcl_buffer_ptr, 4 );
      lcl_vdinput = _mm512_add_ps( lcl_vdinput, _mm512_load_act( doutput_ptr ) );
      _mm512_i32scatter_ps( lcl_buffer_ptr, _mm512_loadu_si512( mask_ptr ), lcl_vdinput, 4 );

      lcl_vdinput2 = _mm512_i32gather_ps( _mm512_loadu_si512( mask_ptr+16 ), lcl_buffer_ptr, 4 );
      lcl_vdinput2 = _mm512_add_ps( lcl_vdinput2, _mm512_load_act( doutput_ptr+16 ) );
      _mm512_i32scatter_ps( lcl_buffer_ptr, _mm512_loadu_si512( mask_ptr+16 ), lcl_vdinput2, 4 );
    }
  }
#endif
#if defined(LIBXSMM_DNN_POOLING_BWD_AVG)
  for( ho = oph; ho < (ofh+oph); ho++ ) {
    hi = ((ho-oph) * sh) - handle->desc.pad_h;
    for( wo = opw; wo < (ofw+opw); wo++ ) {
      wi = ((wo-opw) * sw) - handle->desc.pad_w;
      for( kh = 0; kh < handle->desc.R; kh++ ) {
        if(hi+kh < 0 || hi+kh >= ifh) continue;
        for( kw = 0; kw < handle->desc.S; kw++ ) {
          if(wi+kw < 0 || wi+kw >= ifw) {
            continue;
          } else {
            const element_output_type*   doutput_ptr = &LIBXSMM_VLA_ACCESS(5, doutput,    img, fm,    ho,    wo, 0, nBlocksFm, ofhp, ofwp, 32);
                  float*              lcl_dinput_ptr = &LIBXSMM_VLA_ACCESS(3, lcl_dinput,          hi+kh, wi+kw, 0,                   ifw, 32);
            const __m512 recp_pool_size_ps = _mm512_set1_ps( recp_pool_size );
            const __m512 lcl_dinput_ps  = _mm512_loadu_ps( lcl_dinput_ptr );
            const __m512 lcl_dinput_ps2 = _mm512_loadu_ps( lcl_dinput_ptr+16 );
            _mm512_storeu_ps( lcl_dinput_ptr, _mm512_fmadd_ps( _mm512_load_act( doutput_ptr ), recp_pool_size_ps, lcl_dinput_ps ) );
            _mm512_storeu_ps( lcl_dinput_ptr+16, _mm512_fmadd_ps( _mm512_load_act( doutput_ptr+16 ), recp_pool_size_ps, lcl_dinput_ps2 ) );
          }
        }
      }
    }
  }
#endif

  /* copy the local buffer into dinput activations */
  for( hi = iph; hi < (ifh+iph); hi++ ) {
    for( wi = ipw; wi < (ifw+ipw); wi++ ) {
      element_input_type*     dinput_ptr = &LIBXSMM_VLA_ACCESS(5, dinput,     img, fm,        hi,        wi, 0, nBlocksFm, ifhp, ifwp, 32);
      float*              lcl_dinput_ptr = &LIBXSMM_VLA_ACCESS(3, lcl_dinput,             hi-iph,    wi-ipw, 0,                   ifw, 32);
      _mm512_stream_act( dinput_ptr, _mm512_loadu_ps( lcl_dinput_ptr ) );
      _mm512_stream_act( dinput_ptr+16, _mm512_loadu_ps( lcl_dinput_ptr+16 ) );
    }
  }
}

libxsmm_barrier_wait(handle->barrier, ltid);

# undef _mm512_load_act
# undef _mm512_stream_act
# undef _mm512_store_act
