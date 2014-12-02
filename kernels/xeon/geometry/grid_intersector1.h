// ======================================================================== //
// Copyright 2009-2014 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "grid.h"
#include "common/ray.h"
#include "geometry/filter.h"

namespace embree
{
  struct GridIntersector1
  {
    typedef typename Grid::QuadList Primitive;

    struct Precalculations 
    {
      __forceinline Precalculations (const Ray& ray) 
      {
#if defined (__AVX__)

      /*! load the ray into SIMD registers */
      const Vec3fa ray_rdir = rcp_safe(ray.dir);
      const Vec3fa ray_org_rdir = ray.org*ray_rdir;
      org = avx3f(ray.org.x,ray.org.y,ray.org.z);
      const avx3f dir(ray.dir.x,ray.dir.y,ray.dir.z);
      rdir = avx3f(ray_rdir.x,ray_rdir.y,ray_rdir.z);
      org_rdir = avx3f(ray_org_rdir.x,ray_org_rdir.y,ray_org_rdir.z);
      ray_tnear = ray.tnear;
      //const avxf  ray_tfar(ray.tfar);
      
      /*! offsets to select the side that becomes the lower or upper bound */
      nearX = ray_rdir.x >= 0.0f ? 0*sizeof(ssef) : 1*sizeof(ssef);
      nearY = ray_rdir.y >= 0.0f ? 2*sizeof(ssef) : 3*sizeof(ssef);
      nearZ = ray_rdir.z >= 0.0f ? 4*sizeof(ssef) : 5*sizeof(ssef);

#else

      /*! load the ray into SIMD registers */
      const Vec3fa ray_rdir = rcp_safe(ray.dir);
      const Vec3fa ray_org_rdir = ray.org*ray_rdir;
      org = sse3f(ray.org.x,ray.org.y,ray.org.z);
      const sse3f dir(ray.dir.x,ray.dir.y,ray.dir.z);
      rdir = sse3f(ray_rdir.x,ray_rdir.y,ray_rdir.z);
      org_rdir = sse3f(ray_org_rdir.x,ray_org_rdir.y,ray_org_rdir.z);
      ray_tnear = ray.tnear;
      //const ssef  ray_tfar(ray.tfar);
      
      /*! offsets to select the side that becomes the lower or upper bound */
      nearX = ray_rdir.x >= 0.0f ? 0*sizeof(ssef) : 1*sizeof(ssef);
      nearY = ray_rdir.y >= 0.0f ? 2*sizeof(ssef) : 3*sizeof(ssef);
      nearZ = ray_rdir.z >= 0.0f ? 4*sizeof(ssef) : 5*sizeof(ssef);

#endif
      }

#if defined (__AVX__)
      avx3f org;
      avx3f rdir;
      avx3f org_rdir;
      avxf ray_tnear;
      size_t nearX;
      size_t nearY;
      size_t nearZ;
#else
      sse3f org;
      sse3f rdir;
      sse3f org_rdir;
      ssef ray_tnear;
      size_t nearX;
      size_t nearY;
      size_t nearZ;
#endif
    };

    static __forceinline void intersectFinish (Ray& ray, 
                                               const Vec3fa& q, const Vec3fa& e0, const Vec3fa& e1, const Vec3fa& e2, 
                                               const Vec2f uv, const float U, const float V, const float W,
                                               const Primitive& prim)
      {
        const Vec3fa Ng0 = cross(e1,e0);
        const Vec3fa Ng = Ng0+Ng0;
        const float det = dot(ray.dir,Ng);
        const float rcpDet = rcp(det);
        const float T   = dot(q,Ng);
        const float t = T*rcpDet;
        if (unlikely(ray.tnear <= t && t <= ray.tfar)) {
          //ray.u    = U * rcpDet;
          //ray.v    = V * rcpDet;
          ray.u    = uv.x * rcpDet;
          ray.v    = uv.y * rcpDet;
          ray.tfar = t;
          ray.Ng   = Ng;
	  /*float u = U * rcpDet;
	  float v = V * rcpDet;
	  float w = 1.0f-u-v;
          ray.Ng.x = float(size_t(prim.id)>>4  & 0xFF)/255.0f * (0.5f+0.5f*u);
          ray.Ng.y = float(size_t(prim.id)>>8  & 0xFF)/255.0f * (0.5f+0.5f*v);
          ray.Ng.z = float(size_t(prim.id)>>16 & 0xFF)/255.0f * (0.5f+0.5f*w);*/
          ray.geomID  = prim.grid.geomID;
          ray.primID  = prim.grid.primID;
        }
      }

    __forceinline static void intersectQuad(Ray& ray, const Vec3fa& O, const Vec3fa& D,
                                               const Vec3fa& q00, const Vec3fa& q01, 
                                               const Vec3fa& q10, const Vec3fa& q11,
                                               const Vec2f& uv00, const Vec2f& uv01, 
                                               const Vec2f& uv10, const Vec2f& uv11,
                                               const Primitive& prim)
    {
      const sse3f DDDD(D.x,D.y,D.z);
      sse3f p00; transpose((ssef)q00,(ssef)q01,(ssef)q11,(ssef)q10,p00.x,p00.y,p00.z);
      
      const sse3f t000_start = shuffle<0,1,3,0>(p00), t000_end = shuffle<1,3,0,0>(p00);
      const sse3f e000 = t000_end - t000_start;
      const sse3f s000 = t000_end + t000_start;
      const ssef  u000 = dot(cross(e000,s000),DDDD);
      if (all(ge_mask(Vec3fa(u000),Vec3fa(0.0f))) || all(le_mask(Vec3fa(u000),Vec3fa(0.0f)))) 
        intersectFinish(ray, q00,q01-q00,q10-q01,q00-q10,u000[0]*uv10+u000[1]*uv00+u000[2]*uv01,u000[0],u000[1],u000[2],prim);

      const sse3f t001_start = shuffle<2,3,1,0>(p00), t001_end = shuffle<3,1,2,0>(p00);
      const sse3f e001 = t001_end - t001_start;
      const sse3f s001 = t001_end + t001_start;
      const ssef  u001 = dot(cross(e001,s001),DDDD);
      if (all(ge_mask(Vec3fa(u001),Vec3fa(0.0f))) || all(le_mask(Vec3fa(u001),Vec3fa(0.0f)))) 
        intersectFinish(ray,q11,q10-q11,q01-q10,q11-q01,u001[0]*uv01+u001[1]*uv11+u001[2]*uv10,u001[0],u001[1],u001[2],prim);
    }

#if defined(__AVX__) && 0

    __forceinline static void intersectDualQuad(Ray& ray, const Vec3fa& O, const Vec3fa& D,
                                                   const Vec3fa& q00, const Vec3fa& q01, 
                                                   const Vec3fa& q10, const Vec3fa& q11,
                                                   const Vec3fa& q20, const Vec3fa& q21,
                                                   const Primitive& prim)
    {
      const avx3f D8(D.x,D.y,D.z);

      const avxf q00_q10((ssef)q00,(ssef)q10);
      const avxf q01_q11((ssef)q01,(ssef)q11);
      const avxf q11_q21((ssef)q11,(ssef)q21);
      const avxf q10_q20((ssef)q10,(ssef)q20);
      avx3f p00_p10; transpose(q00_q10,q01_q11,q11_q21,q10_q20,p00_p10.x,p00_p10.y,p00_p10.z);

      const avx3f t000_t100_start = shuffle<0,1,3,0>(p00_p10), t000_t100_end = shuffle<1,3,0,0>(p00_p10);
      const avx3f e000_e100 = t000_t100_end - t000_t100_start;
      const avx3f s000_s100 = t000_t100_end + t000_t100_start;
      const avxf  u000_u100 = dot(cross(e000_e100,s000_s100),D8);
      if (all(ge_mask(Vec3fa(extract<0>(u000_u100)),Vec3fa(0.0f))) || all(le_mask(Vec3fa(extract<0>(u000_u100)),Vec3fa(0.0f)))) 
        intersectFinish(ray,q00,q01-q00,q10-q01,q00-q10,u000_u100[0],u000_u100[1],u000_u100[2],prim);
      if (all(ge_mask(Vec3fa(extract<1>(u000_u100)),Vec3fa(0.0f))) || all(le_mask(Vec3fa(extract<1>(u000_u100)),Vec3fa(0.0f)))) 
        intersectFinish(ray,q10,q11-q10,q20-q11,q10-q20,u000_u100[4],u000_u100[5],u000_u100[6],prim);
      
      const avx3f t001_t101_start = shuffle<2,3,1,0>(p00_p10), t001_t101_end = shuffle<3,1,2,0>(p00_p10);
      const avx3f e001_e101 = t001_t101_end - t001_t101_start;
      const avx3f s001_s101 = t001_t101_end + t001_t101_start;
      const avxf  u001_u101 = dot(cross(e001_e101,s001_s101),D8);
      if (all(ge_mask(Vec3fa(extract<0>(u001_u101)),Vec3fa(0.0f))) || all(le_mask(Vec3fa(extract<0>(u001_u101)),Vec3fa(0.0f)))) 
        intersectFinish2(ray,q11,q10-q11,q01-q10,q11-q01,u001_u101[0],u001_u101[1],u001_u101[2],prim);
      if (all(ge_mask(Vec3fa(extract<1>(u001_u101)),Vec3fa(0.0f))) || all(le_mask(Vec3fa(extract<1>(u001_u101)),Vec3fa(0.0f)))) 
        intersectFinish2(ray,q21,q20-q21,q11-q20,q21-q11,u001_u101[4],u001_u101[5],u001_u101[6],prim);
    }

#endif

    static __forceinline void intersectQuad (Ray& ray, 
					     const Vec3fa& v00, const Vec3fa& v10,
					     const Vec3fa& v01, const Vec3fa& v11,
					     const Vec2f& uv00, const Vec2f& uv10,
					     const Vec2f& uv01, const Vec2f& uv11,
					     const Primitive& prim)
    {
      const Vec3fa O = ray.org;
      const Vec3fa D = ray.dir;
      const Vec3fa q00 = v00-O, q10 = v10-O;
      const Vec3fa q01 = v01-O, q11 = v11-O;
      intersectQuad(ray,O,D, q00,q01,q10,q11, uv00,uv01,uv10,uv11, prim);
    }

    static __forceinline void intersectQuadQuad (Ray& ray, 
                                                 const Vec3fa& v00, const Vec3fa& v10, const Vec3fa& v20,
                                                 const Vec3fa& v01, const Vec3fa& v11, const Vec3fa& v21,
                                                 const Vec3fa& v02, const Vec3fa& v12, const Vec3fa& v22,
                                                 const Vec2f& uv00, const Vec2f& uv10, const Vec2f& uv20,
                                                 const Vec2f& uv01, const Vec2f& uv11, const Vec2f& uv21,
                                                 const Vec2f& uv02, const Vec2f& uv12, const Vec2f& uv22,
                                                 const Primitive& prim)
    {
#if defined(__AVX__) && 0

      const Vec3fa O = ray.org;
      const Vec3fa D = ray.dir;
      const Vec3fa q00 = v00-O, q10 = v10-O, q20 = v20-O;
      const Vec3fa q01 = v01-O, q11 = v11-O, q21 = v21-O;
      const Vec3fa q02 = v02-O, q12 = v12-O, q22 = v22-O;
      intersectDualQuad(ray,O,D,q00,q01,q10,q11,q20,q21,prim);
      intersectDualQuad(ray,O,D,q01,q02,q11,q12,q21,q22,prim);

#else

      const Vec3fa O = ray.org;
      const Vec3fa D = ray.dir;
      const Vec3fa q00 = v00-O, q10 = v10-O, q20 = v20-O;
      const Vec3fa q01 = v01-O, q11 = v11-O, q21 = v21-O;
      const Vec3fa q02 = v02-O, q12 = v12-O, q22 = v22-O;
      intersectQuad(ray,O,D, q00,q01,q10,q11, uv00,uv01,uv10,uv11, prim);
      intersectQuad(ray,O,D, q01,q02,q11,q12, uv01,uv02,uv11,uv12, prim);
      intersectQuad(ray,O,D, q10,q11,q20,q21, uv10,uv11,uv20,uv21, prim);
      intersectQuad(ray,O,D, q11,q12,q21,q22, uv11,uv12,uv21,uv22, prim);

#endif
    }
    
    /*! Intersect a ray with the triangle and updates the hit. */
    static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Primitive& prim, const void* geom, size_t& lazy_node)
    {
      STAT3(normal.trav_prims,1,1,1);

#if defined (__AVX__)

      /* perform box tests */
      const avxf ray_tfar(ray.tfar);
      size_t mask = prim.bounds.intersect<false>(pre.nearX, pre.nearY, pre.nearZ, pre.org, pre.rdir, pre.org_rdir, pre.ray_tnear, ray_tfar);

#else

      /* perform box tests */
      const ssef ray_tfar(ray.tfar);
      size_t mask = prim.bounds.intersect<false>(pre.nearX, pre.nearY, pre.nearZ, pre.org, pre.rdir, pre.org_rdir, pre.ray_tnear, ray_tfar);

#endif

      /* intersect quad-quads */
      while (mask) 
      {
        const size_t i = __bscf(mask);
	const size_t ofs = prim.quads[i].ofs;
	switch (prim.quads[i].type) {
	case Grid::QuadList::Quads::QUAD1X1: {
	  const Vec3fa& v00 = prim.grid.point(ofs,0), v10 = (&v00)[1];
	  const Vec3fa& v01 = prim.grid.point(ofs,1), v11 = (&v01)[1];
	  const Vec2f& uv00 = prim.grid.uvs  (ofs,0), uv10 = (&uv00)[1];
	  const Vec2f& uv01 = prim.grid.uvs  (ofs,1), uv11 = (&uv01)[1];
	  intersectQuad(ray, v00,v10,v01,v11, uv00,uv10,uv01,uv11, prim);
	  break;
	}
	case Grid::QuadList::Quads::QUAD1X2: {
	  const Vec3fa& v00 = prim.grid.point(ofs,0), v10 = (&v00)[1];
	  const Vec3fa& v01 = prim.grid.point(ofs,1), v11 = (&v01)[1];
	  const Vec3fa& v02 = prim.grid.point(ofs,2), v12 = (&v02)[1];
	  const Vec2f& uv00 = prim.grid.uvs  (ofs,0), uv10 = (&uv00)[1];
	  const Vec2f& uv01 = prim.grid.uvs  (ofs,1), uv11 = (&uv01)[1];
	  const Vec2f& uv02 = prim.grid.uvs  (ofs,2), uv12 = (&uv02)[1];
	  intersectQuad(ray, v00,v10,v01,v11, uv00,uv10,uv01,uv11, prim);
	  intersectQuad(ray, v01,v11,v02,v12, uv01,uv11,uv02,uv12, prim);
	  break;
	}
	case Grid::QuadList::Quads::QUAD2X1: {
	  const Vec3fa& v00 = prim.grid.point(ofs,0), v10 = (&v00)[1], v20 = (&v00)[2];
	  const Vec3fa& v01 = prim.grid.point(ofs,1), v11 = (&v01)[1], v21 = (&v01)[2];
	  const Vec2f& uv00 = prim.grid.uvs  (ofs,0), uv10 = (&uv00)[1], uv20 = (&uv00)[2];
	  const Vec2f& uv01 = prim.grid.uvs  (ofs,1), uv11 = (&uv01)[1], uv21 = (&uv01)[2];
	  intersectQuad(ray, v00,v10,v01,v11, uv00,uv10,uv01,uv11, prim);
	  intersectQuad(ray, v10,v20,v11,v21, uv10,uv20,uv11,uv21, prim);
	  break;
	}
	case Grid::QuadList::Quads::QUAD2X2: {
	  const Vec3fa& v00 = prim.grid.point(ofs,0), v10 = (&v00)[1], v20 = (&v00)[2];
	  const Vec3fa& v01 = prim.grid.point(ofs,1), v11 = (&v01)[1], v21 = (&v01)[2];
	  const Vec3fa& v02 = prim.grid.point(ofs,2), v12 = (&v02)[1], v22 = (&v02)[2];
	  const Vec2f& uv00 = prim.grid.uvs  (ofs,0), uv10 = (&uv00)[1], uv20 = (&uv00)[2];
	  const Vec2f& uv01 = prim.grid.uvs  (ofs,1), uv11 = (&uv01)[1], uv21 = (&uv01)[2];
	  const Vec2f& uv02 = prim.grid.uvs  (ofs,2), uv12 = (&uv02)[1], uv22 = (&uv02)[2];
	  intersectQuadQuad(ray,
			    v00,v10,v20,v01,v11,v21,v02,v12,v22,
			    uv00,uv10,uv20,uv01,uv11,uv21,uv02,uv12,uv22,
			    prim);
	  break;
	}
	default: assert(false);  
	}
      }
    }

    /*! Intersect a ray with the triangle and updates the hit. */
    static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Primitive* prim, size_t ty, const void* geom, size_t& lazy_node) {
      intersect(pre,ray,prim[0],geom,lazy_node);
    }
    
    /*! Test if the ray is occluded by the primitive */
    static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Primitive& prim, const void* geom, size_t& lazy_node)
    {
      STAT3(shadow.trav_prims,1,1,1);
      return false;
    }

    /*! Test if the ray is occluded by the primitive */
    static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Primitive* prim, size_t ty, const void* geom, size_t& lazy_node) {
      return occluded(pre,ray,prim[0],geom,lazy_node);
    }
  };
}