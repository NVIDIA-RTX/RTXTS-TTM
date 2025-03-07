# RTX Texture Streaming

The RTX Texture Streaming (RTXTS) library is designed to manage and orchestrate tile allocation and streaming for sparse textures. Library functionality is tightly coupled with the Direct3D 12 concepts of [Tiled Resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_tiled_resources_tier) and [Sampler Feedback](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html). Tiled resources can be much more memory-efficient compared than regular textures as their memory usage can be managed with a finer granularity. This selective residency reduces memory usage and may enhance performance, particularly in cases involving ultra-high-resolution textures in dense scenes.

## Key Features and Benefits

* Lightweight CPU-side utility library
* Utilizes sampler feedback data to build and update a list of tiles needing allocation or eviction
* Assists with tiled resource creation and management
* Drives heap allocations/deallocations
* Configurable tile allocation timeout
* Optionally generates data for MinMip texture

## Sample

An sample using this library can be found here: https://github.com/NVIDIA-RTX/RTXTS
 
## Distribution

RTXTS is distributed in full sources along with an integration guide.

## Support

E-mail: rtxts-sdk-support@nvidia.com

## License

[NVIDIA RTX SDKs LICENSE](license.txt)
