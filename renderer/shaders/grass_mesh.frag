#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#define GBUF_PASS 1
#define GBUF_NO_PREPASS 0
#include "grass_mesh_impl.frag.inc"
