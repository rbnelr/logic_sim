#pragma once

#if 0

#endif
/*
	struct WirePoints {
		float2[] points;
	}

	struct Part {
		string name;
		float2 size;
	
		int inputs;  // no of input pins
		int outputs; // no of output pins
	
		// how many total outputs are used (recursively)
		// and thus how many state vars need to be allocated
		// when this part is placed in the simulation
		int state_count; // -1 if stale?
	
		// list of all used parts in this part
		// also contains inputs and outputs
		struct Subpart {
			Part*  type;
		
			float2 pos;
			int    rot;
			float  scale;
		
			struct Input {
				int subpart_idx;
				int     pin_idx;
				int   state_idx;
			
				float2[] wire_points;
			};
			int[] inputs;
		};
		Part*[] subparts;
	}

	uint8_t state[];
*/

