# Tiled Texture Manager Library

The Tiled Texture Manager (TTM) library is designed to manage and orchestrate tile allocation and streaming for sparse textures. Library functionality is tightly coupled with the Direct3D 12 concepts of [Tiled Resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_tiled_resources_tier) and [Sampler Feedback](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html). Tiled resources can be much more memory-efficient compared than regular textures as their memory usage can be managed with a finer granularity. This selective residency reduces memory usage and may enhance performance, particularly in cases involving ultra-high-resolution textures in dense scenes.

## Key Features and Benefits

* Lightweight CPU-side utility library
* Utilizes sampler feedback data to build and update a list of tiles needing allocation or eviction
* Assists with tiled resource creation and management
* Drives heap allocations/deallocations
* Configurable tile allocation timeout
* Optionally generates data for MinMip texture

## Sample and Documentation

Please refer to the RTXTS SDK repo at https://github.com/NVIDIA-RTX/RTXTS to access the sample and relevant documentation.

## License

[NVIDIA RTX SDKs LICENSE](license.txt)
