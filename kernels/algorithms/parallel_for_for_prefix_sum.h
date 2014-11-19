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

#include "parallel_for_for.h"

namespace embree
{
  template<typename ArrayArray>
    class ParallelForForPrefixSumState : public ParallelForForState<ArrayArray>
  {
  public:
    ParallelForForPrefixSumState ( ArrayArray& array2, const size_t minStepSize ) 
      : ParallelForForState<ArrayArray>(array2,minStepSize), _size(0), _blocks(0), scheduler(LockStepTaskScheduler::instance())
    {
      _blocks  = min((this->K+this->minStepSize-1)/this->minStepSize,scheduler->getNumThreads());
      counts.resize(_blocks);
      sums.resize(_blocks);
    }

    size_t size() const {
      return _size;
    }

  public:
    size_t _size, _blocks;
    std::vector<size_t> counts;
    std::vector<size_t> sums;
    LockStepTaskScheduler* scheduler;
  };

  template<typename ArrayArray, typename Func>
    class ParallelForForPrefixSumTask
  {
  public:
    
    ParallelForForPrefixSumTask (ParallelForForPrefixSumState<ArrayArray>& state, const Func& f) 
      : state(state), f(f)
    {
      const size_t blocks = state._blocks;
      state.scheduler->dispatchTaskSet(task_execute,this,blocks);
      
      /* calculate prefix sum */
      size_t sum=0;
      for (size_t i=0; i<blocks; i++)
      {
        const size_t c = state.counts[i];
        state.sums[i] = sum;
        sum+=c;
      }
      state._size = sum;
    }
    
    void execute(const size_t threadIndex, const size_t threadCount, const size_t taskIndex, const size_t taskCount) 
    {
      /* calculate range */
      const size_t k0 = (taskIndex+0)*state.K/taskCount;
      const size_t k1 = (taskIndex+1)*state.K/taskCount;
      size_t i0, j0; state.start_indices(k0,i0,j0);
      
      /* iterate over arrays */
      size_t k=k0, N=0;
      for (size_t i=i0; k<k1; i++) {
        const size_t r0 = j0, r1 = min(state.sizes[i],r0+k1-k);
        if (r1 > r0) N += f(state.array2[i],range<size_t>(r0,r1),state.sums[taskIndex]+N);
        k+=r1-r0; j0 = 0;
      }
      state.counts[taskIndex] = N;
    }
    
    static void task_execute(void* data, const size_t threadIndex, const size_t threadCount, const size_t taskIndex, const size_t taskCount) {
      ((ParallelForForPrefixSumTask*)data)->execute(threadIndex,threadCount,taskIndex,taskCount);
    }
    
  private:
    ParallelForForPrefixSumState<ArrayArray>& state;
    const Func& f;
  };
  
  template<typename ArrayArray, typename Func>
    __forceinline void parallel_for_for_prefix_sum( ParallelForForPrefixSumState<ArrayArray>& state, const Func& f)
  {
    ParallelForForPrefixSumTask<ArrayArray,Func>(state,f);
  }
}