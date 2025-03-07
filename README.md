## Overview

RTX Texture Streaming (RTXTS) library is a specialized module designed to efficiently manage and orchestrate texture tiles allocation and streaming for sparse Direct3D12 textures. Library functionality is tightly coupled with Direct3D12 concept of tiled resources and sampler feedback functionality [https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html]. Tiled resources are more memory-efficient compared to regular textures, which need all memory allocated during creation. With tiled resources, GPU memory can be managed with fine granularity. This selective residency method reduces memory usage and enhances performance, particularly in cases involving ultra-high-resolution or large textures.

## Key Features and Benefits

* Lightweight CPU-side utility library
* Utilizes sampler feedback data to build and update a list of tiles needing mapping or eviction
* Assists with resource creation
* Drives heaps allocations/deallocations
* Optionally generates data for MinMip texture

### Extensibility and Flexibility  
RTXTS library provides a clear and robust API that enables developers to integrate custom streaming logic and memory management polices.

### Optimized Memory Usage
By loading only the required portions of a texture, the library reduces the overall memory footprint, allowing for higher resolution textures and more complex scenes.

### Improved Performance
Dynamic tile management leads to faster load times and smoother transitions, which is critical for games and other real-time applications.

### Scalability
The approach scales well with the complexity of the scene. Whether you’re dealing with a small scene or a vast open world, the library can be adapted to the available GPU memory pool, ensuring consistent performance.
 
## Distribution
RTXTS is distributed in full sources along with [integration guide][RtxtsIntegrationGuide].

[RtxtsIntegrationGuide]: ./docs/Integration.md

## Support

E-mail: rtxts-sdk-support@nvidia.com

## License

[NVIDIA RTX SDKs LICENSE](license.txt)
