#pragma once

#include "shared_includes.h"

namespace fc
{
    template<typename T>
	class queue
	{
	public:
		u32 cached_prod;
		u32 cached_cons;
		u32 mask;
		u32 size;
		u32 *producer;
		u32 *consumer;
		T *ring;
		u8 *map;

		// return the number of free entries in producer ring
		inline u32 nb_free()
		{
			cached_cons = *consumer + size;
			return cached_cons - cached_prod;
		}

		// return the number of usable entries in consumer ring
		inline u32 nb_avail() {
			cached_prod = *producer;
			return cached_prod - cached_cons;
		}

		// dequeue descriptors from consumer ring
		inline u32 deq(T* descs, u32 nb)
		{
			if(nb_avail() < nb)
			{
				return -1;
			}

			u_smp_rmb();

			for(u32 i=0; i<nb; i++)
			{
				u32 idx = cached_cons & mask;
				cached_cons++;
				descs[i] = ring[idx];
			}

			u_smp_wmb();
			*consumer = cached_cons;

			return nb;
		}

		// enqueue descriptors into the producer ring
		inline u32 enq(T* descs, u32 nb)
		{
			if(nb_free() < nb)
			{
				return -1;
			}

			for(u32 i=0; i<nb; i++)
			{
				u32 idx = cached_prod & mask;
				cached_prod++;
				ring[idx] = descs[i];
			}		

			u_smp_wmb();

			*producer = cached_prod;

			return nb;
		}
	};

	// the queue for rx ring and tx ring
	typedef queue<struct xdp_desc> xdp_queue;
	// the queue for fill ring and complete ring
	typedef queue<u64> umem_queue;
}