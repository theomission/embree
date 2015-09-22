// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
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

#include "bvh4_intersector4_chunk.h"
#include "bvh4_intersector_node.h"

#include "../geometry/triangle4.h"
#include "../geometry/triangle4i.h"
#include "../geometry/triangle4v.h"
#include "../geometry/triangle4v_mb.h"
#include "../geometry/triangle8.h"
#include "../geometry/intersector_iterators.h"
#include "../geometry/bezier1v_intersector.h"
#include "../geometry/bezier1i_intersector.h"
#include "../geometry/triangle_intersector_moeller.h"
#include "../geometry/triangle_intersector_pluecker.h"
#include "../geometry/triangle4i_intersector_pluecker.h"
#include "../geometry/object_intersector4.h"

namespace embree
{
  namespace isa
  {
    template<int types, bool robust, typename PrimitiveIntersector4>
    void BVH4Intersector4Chunk<types,robust,PrimitiveIntersector4>::intersect(vbool4* valid_i, BVH4* bvh, Ray4& ray)
    {
      /* verify correct input */
      vbool4 valid0 = *valid_i;
#if defined(RTCORE_IGNORE_INVALID_RAYS)
      valid0 &= ray.valid();
#endif
      assert(all(valid0,ray.tnear > -FLT_MIN));
      assert(!(types & BVH4::FLAG_NODE_MB) || all(valid0,ray.time >= 0.0f & ray.time <= 1.0f));

      /* load ray */
      const Vec3vf4 rdir = rcp_safe(ray.dir);
      const Vec3vf4 org(ray.org), org_rdir = org * rdir;
      vfloat4 ray_tnear = select(valid0,ray.tnear,vfloat4(pos_inf));
      vfloat4 ray_tfar  = select(valid0,ray.tfar ,vfloat4(neg_inf));
      const vfloat4 inf = vfloat4(pos_inf);
      Precalculations pre(valid0,ray);
      
      /* allocate stack and push root node */
      vfloat4    stack_near[stackSize];
      NodeRef stack_node[stackSize];
      stack_node[0] = BVH4::invalidNode;
      stack_near[0] = inf;
      stack_node[1] = bvh->root;
      stack_near[1] = ray_tnear; 
      NodeRef* stackEnd = stack_node+stackSize;
      NodeRef* __restrict__ sptr_node = stack_node + 2;
      vfloat4*    __restrict__ sptr_near = stack_near + 2;
      
      while (1)
      {
        /* pop next node from stack */
        assert(sptr_node > stack_node);
        sptr_node--;
        sptr_near--;
        NodeRef cur = *sptr_node;
        if (unlikely(cur == BVH4::invalidNode)) {
          assert(sptr_node == stack_node);
          break;
        }
        
        /* cull node if behind closest hit point */
        vfloat4 curDist = *sptr_near;
        if (unlikely(none(ray_tfar > curDist))) 
          continue;
        
        while (1)
        {
	  /* process normal nodes */
          if (likely((types & 0x1) && cur.isNode()))
          {
	    const vbool4 valid_node = ray_tfar > curDist;
	    STAT3(normal.trav_nodes,1,popcnt(valid_node),8);
	    const Node* __restrict__ const node = cur.node();
	    
	    /* pop of next node */
	    assert(sptr_node > stack_node);
	    sptr_node--;
	    sptr_near--;
	    cur = *sptr_node; 
	    curDist = *sptr_near;
          
#pragma unroll(4)
	    for (unsigned i=0; i<BVH4::N; i++)
	    {
	      const NodeRef child = node->children[i];
	      if (unlikely(child == BVH4::emptyNode)) break;
	      vfloat4 lnearP; const vbool4 lhit = intersect_node<robust>(node,i,org,rdir,org_rdir,ray_tnear,ray_tfar,lnearP);
	      	      
	      /* if we hit the child we choose to continue with that child if it 
		 is closer than the current next child, or we push it onto the stack */
	      if (likely(any(lhit)))
	      {
		assert(sptr_node < stackEnd);
		const vfloat4 childDist = select(lhit,lnearP,inf);
		const NodeRef child = node->children[i];
		assert(child != BVH4::emptyNode);
		sptr_node++;
		sptr_near++;
		
		/* push cur node onto stack and continue with hit child */
		if (any(childDist < curDist))
		{
		  *(sptr_node-1) = cur;
		  *(sptr_near-1) = curDist; 
		  curDist = childDist;
		  cur = child;
		}
		
		/* push hit child onto stack */
		else {
		  *(sptr_node-1) = child;
		  *(sptr_near-1) = childDist; 
		}
	      }	      
	    }
	  }
	  /* process motion blur nodes */
          else if (likely((types & 0x10) && cur.isNodeMB()))
	  {
	    const vbool4 valid_node = ray_tfar > curDist;
	    STAT3(normal.trav_nodes,1,popcnt(valid_node),8);
	    const BVH4::NodeMB* __restrict__ const node = cur.nodeMB();
          
	    /* pop of next node */
	    assert(sptr_node > stack_node);
	    sptr_node--;
	    sptr_near--;
	    cur = *sptr_node; 
	    curDist = *sptr_near;
	    
#pragma unroll(4)
	    for (unsigned i=0; i<BVH4::N; i++)
	    {
	      const NodeRef child = node->child(i);
	      if (unlikely(child == BVH4::emptyNode)) break;
	      vfloat4 lnearP; const vbool4 lhit = intersect_node(node,i,org,rdir,org_rdir,ray_tnear,ray_tfar,ray.time,lnearP);
	      
	      /* if we hit the child we choose to continue with that child if it 
		 is closer than the current next child, or we push it onto the stack */
	      if (likely(any(lhit)))
	      {
		assert(sptr_node < stackEnd);
		assert(child != BVH4::emptyNode);
		const vfloat4 childDist = select(lhit,lnearP,inf);
		sptr_node++;
		sptr_near++;
		
		/* push cur node onto stack and continue with hit child */
		if (any(childDist < curDist))
		{
		  *(sptr_node-1) = cur;
		  *(sptr_near-1) = curDist; 
		  curDist = childDist;
		  cur = child;
		}
		
		/* push hit child onto stack */
		else {
		  *(sptr_node-1) = child;
		  *(sptr_near-1) = childDist; 
		}
	      }	      
	    }
	  }
	  else 
	    break;
        }
        
        /* return if stack is empty */
        if (unlikely(cur == BVH4::invalidNode)) {
          assert(sptr_node == stack_node);
          break;
        }
        
        /* intersect leaf */
	assert(cur != BVH4::emptyNode);
        const vbool4 valid_leaf = ray_tfar > curDist;
        STAT3(normal.trav_leaves,1,popcnt(valid_leaf),4);
        size_t items; const Primitive* prim = (Primitive*) cur.leaf(items);

        size_t lazy_node = 0;
        PrimitiveIntersector4::intersect(valid_leaf,pre,ray,prim,items,bvh->scene,lazy_node);
        ray_tfar = select(valid_leaf,ray.tfar,ray_tfar);

        if (unlikely(lazy_node)) {
          *sptr_node = lazy_node; sptr_node++;
          *sptr_near = neg_inf;   sptr_near++;
        }
      }
      AVX_ZERO_UPPER();
    }
    
    template<int types, bool robust, typename PrimitiveIntersector4>
    void BVH4Intersector4Chunk<types,robust,PrimitiveIntersector4>::occluded(vbool4* valid_i, BVH4* bvh, Ray4& ray)
    {
      /* verify correct input */
      vbool4 valid = *valid_i;
#if defined(RTCORE_IGNORE_INVALID_RAYS)
      valid &= ray.valid();
#endif
      assert(all(valid,ray.tnear > -FLT_MIN));
      assert(!(types & BVH4::FLAG_NODE_MB) || all(valid,ray.time >= 0.0f & ray.time <= 1.0f));

      /* load ray */
      vbool4 terminated = !valid;
      const Vec3vf4 rdir = rcp_safe(ray.dir);
      const Vec3vf4 org(ray.org), org_rdir = org * rdir;
      vfloat4 ray_tnear = select(valid,ray.tnear,vfloat4(pos_inf));
      vfloat4 ray_tfar  = select(valid,ray.tfar ,vfloat4(neg_inf));
      const vfloat4 inf = vfloat4(pos_inf);
      Precalculations pre(valid,ray);

      /* allocate stack and push root node */
      vfloat4    stack_near[stackSize];
      NodeRef stack_node[stackSize];
      stack_node[0] = BVH4::invalidNode;
      stack_near[0] = inf;
      stack_node[1] = bvh->root;
      stack_near[1] = ray_tnear; 
      NodeRef* stackEnd = stack_node+stackSize;
      NodeRef* __restrict__ sptr_node = stack_node + 2;
      vfloat4*    __restrict__ sptr_near = stack_near + 2;
      
      while (1)
      {
        /* pop next node from stack */
        assert(sptr_node > stack_node);
        sptr_node--;
        sptr_near--;
        NodeRef cur = *sptr_node;
        if (unlikely(cur == BVH4::invalidNode)) {
          assert(sptr_node == stack_node);
          break;
        }
        
        /* cull node if behind closest hit point */
        vfloat4 curDist = *sptr_near;
        if (unlikely(none(ray_tfar > curDist))) 
          continue;
        
        while (1)
        {
	  /* process normal nodes */
          if (likely((types & 0x1) && cur.isNode()))
          {
	    const vbool4 valid_node = ray_tfar > curDist;
	    STAT3(normal.trav_nodes,1,popcnt(valid_node),8);
	    const Node* __restrict__ const node = cur.node();
	    
	    /* pop of next node */
	    assert(sptr_node > stack_node);
	    sptr_node--;
	    sptr_near--;
	    cur = *sptr_node; 
	    curDist = *sptr_near;
          
#pragma unroll(4)
	    for (unsigned i=0; i<BVH4::N; i++)
	    {
	      const NodeRef child = node->children[i];
	      if (unlikely(child == BVH4::emptyNode)) break;
	      vfloat4 lnearP; const vbool4 lhit = intersect_node<robust>(node,i,org,rdir,org_rdir,ray_tnear,ray_tfar,lnearP);
	      
	      /* if we hit the child we choose to continue with that child if it 
		 is closer than the current next child, or we push it onto the stack */
	      if (likely(any(lhit)))
	      {
		assert(sptr_node < stackEnd);
		const vfloat4 childDist = select(lhit,lnearP,inf);
		const NodeRef child = node->children[i];
		assert(child != BVH4::emptyNode);
		sptr_node++;
		sptr_near++;
		
		/* push cur node onto stack and continue with hit child */
		if (any(childDist < curDist))
		{
		  *(sptr_node-1) = cur;
		  *(sptr_near-1) = curDist; 
		  curDist = childDist;
		  cur = child;
		}
		
		/* push hit child onto stack */
		else {
		  *(sptr_node-1) = child;
		  *(sptr_near-1) = childDist; 
		}
	      }	      
	    }
	  }
	  /* process motion blur nodes */
          else if (likely((types & 0x10) && cur.isNodeMB()))
	  {
	    const vbool4 valid_node = ray_tfar > curDist;
	    STAT3(normal.trav_nodes,1,popcnt(valid_node),8);
	    const BVH4::NodeMB* __restrict__ const node = cur.nodeMB();
          
	    /* pop of next node */
	    assert(sptr_node > stack_node);
	    sptr_node--;
	    sptr_near--;
	    cur = *sptr_node; 
	    curDist = *sptr_near;
	    
#pragma unroll(4)
	    for (unsigned i=0; i<BVH4::N; i++)
	    {
	      const NodeRef child = node->child(i);
	      if (unlikely(child == BVH4::emptyNode)) break;
	      vfloat4 lnearP; const vbool4 lhit = intersect_node(node,i,org,rdir,org_rdir,ray_tnear,ray_tfar,ray.time,lnearP);
	      
	      /* if we hit the child we choose to continue with that child if it 
		 is closer than the current next child, or we push it onto the stack */
	      if (likely(any(lhit)))
	      {
		assert(sptr_node < stackEnd);
		assert(child != BVH4::emptyNode);
		const vfloat4 childDist = select(lhit,lnearP,inf);
		sptr_node++;
		sptr_near++;
		
		/* push cur node onto stack and continue with hit child */
		if (any(childDist < curDist))
		{
		  *(sptr_node-1) = cur;
		  *(sptr_near-1) = curDist; 
		  curDist = childDist;
		  cur = child;
		}
		
		/* push hit child onto stack */
		else {
		  *(sptr_node-1) = child;
		  *(sptr_near-1) = childDist; 
		}
	      }	      
	    }
	  }
	  else 
	    break;
        }
        
        /* return if stack is empty */
        if (unlikely(cur == BVH4::invalidNode)) {
          assert(sptr_node == stack_node);
          break;
        }
        
        /* intersect leaf */
	assert(cur != BVH4::emptyNode);
        const vbool4 valid_leaf = ray_tfar > curDist;
        STAT3(shadow.trav_leaves,1,popcnt(valid_leaf),4);
        size_t items; const Primitive* prim = (Primitive*) cur.leaf(items);

        size_t lazy_node = 0;
        terminated |= PrimitiveIntersector4::occluded(!terminated,pre,ray,prim,items,bvh->scene,lazy_node);
        if (all(terminated)) break;
        ray_tfar = select(terminated,vfloat4(neg_inf),ray_tfar);

        if (unlikely(lazy_node)) {
          *sptr_node = lazy_node; sptr_node++;
          *sptr_near = neg_inf;   sptr_near++;
        }
      }
      vint4::store(valid & terminated,&ray.geomID,0);
      AVX_ZERO_UPPER();
    }
    
    DEFINE_INTERSECTOR4(BVH4Bezier1vIntersector4Chunk, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<Bezier1vIntersectorN<Ray4> > >);
    DEFINE_INTERSECTOR4(BVH4Bezier1iIntersector4Chunk, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<Bezier1iIntersectorN<Ray4> > >);
    DEFINE_INTERSECTOR4(BVH4Triangle4Intersector4ChunkMoeller, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<TriangleNIntersectorMMoellerTrumbore<Ray4 COMMA Triangle4 COMMA true> > >);
    DEFINE_INTERSECTOR4(BVH4Triangle4Intersector4ChunkMoellerNoFilter, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<TriangleNIntersectorMMoellerTrumbore<Ray4 COMMA Triangle4 COMMA false> > >);
#if defined (__AVX__)
    DEFINE_INTERSECTOR4(BVH4Triangle8Intersector4ChunkMoeller, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<TriangleNIntersectorMMoellerTrumbore<Ray4 COMMA Triangle8 COMMA true> > >);
    DEFINE_INTERSECTOR4(BVH4Triangle8Intersector4ChunkMoellerNoFilter, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<TriangleNIntersectorMMoellerTrumbore<Ray4 COMMA Triangle8 COMMA false> > >);
#endif
    DEFINE_INTERSECTOR4(BVH4Triangle4vIntersector4ChunkPluecker, BVH4Intersector4Chunk<0x1 COMMA true COMMA ArrayIntersector4<TriangleNvIntersectorMPluecker<Ray4 COMMA Triangle4v COMMA true> > >);
    DEFINE_INTERSECTOR4(BVH4Triangle4iIntersector4ChunkPluecker, BVH4Intersector4Chunk<0x1 COMMA true COMMA ArrayIntersector4<Triangle4iIntersectorMPluecker<Ray4 COMMA true> > >);
    DEFINE_INTERSECTOR4(BVH4VirtualIntersector4Chunk, BVH4Intersector4Chunk<0x1 COMMA false COMMA ArrayIntersector4<ObjectIntersector4> >);

    DEFINE_INTERSECTOR4(BVH4Triangle4vMBIntersector4ChunkMoeller, BVH4Intersector4Chunk<0x10 COMMA false COMMA ArrayIntersector4<TriangleNMblurIntersectorMMoellerTrumbore<Ray4 COMMA Triangle4vMB COMMA true> > >);
  }
}
