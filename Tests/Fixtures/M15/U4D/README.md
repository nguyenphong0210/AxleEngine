# M15 U4-D Editor fixture

Open `U4D.axleproj` in AxleEditor. The scene contains one smooth, metallic sphere (metallic 1, roughness 0.2) in a directional color-gradient HDR environment, with no sun or other occluders. The small `gradient.hdr` is a deterministic flat RGBE fixture, with radiance 2 + 1.5 times the direction in each channel, matching the native environment test generator.

Focus **Unoccluded mirror sphere** in the viewport. Compare GTAO and Contact Shadows enabled versus disabled in the render settings. Expected: a smooth reflected gradient, no triangle bands, no blackening of the unoccluded reflection, and no invalid pixels. Contact changes have no effect without a sun. Save an actual Editor viewport capture as visual evidence; native test PPMs only prove the native harness path.

Automated CPU and D3D12/Vulkan evidence is recorded in `docs/reviews/M15_U4_BATCH_4_CODEX_REVIEW.md`. The required interactive screenshot remains open when the capture host is unavailable.
