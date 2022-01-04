#ifndef GPU_SHADER
#  include "gpu_shader_shared_utils.h"
#endif

struct ViewInfos {
  /* View matrices */
  float4x4 persmat;
  float4x4 persinv;
  float4x4 viewmat;
  float4x4 viewinv;
  float4x4 winmat;
  float4x4 wininv;

  float4 clipplanes[6];
  float4 viewvecs[2];
  /* Should not be here. Not view dependent (only main view). */
  float4 viewcamtexcofac;
};

/* TODO(fclem) Mass rename. */
#define ViewProjectionMatrix drw_view.persmat
#define ViewProjectionMatrixInverse drw_view.persinv
#define ViewMatrix drw_view.viewmat
#define ViewMatrixInverse drw_view.viewinv
#define ProjectionMatrix drw_view.winmat
#define ProjectionMatrixInverse drw_view.wininv
#define clipPlanes drw_view.clipplanes
#define ViewVecs drw_view.viewvecs
#define CameraTexCoFactors drw_view.viewcamtexcofac

struct ObjectMatrices {
  float4x4 drw_modelMatrix;
  float4x4 drw_modelMatrixInverse;
};
