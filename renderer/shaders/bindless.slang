struct Bindless
{
  [vk::binding(0)]
  __DynamicResource<__DynamicResourceKind.General> textures[];
  [vk::binding(1)]
  __DynamicResource<__DynamicResourceKind.Sampler> samplers[];
}

[vk::binding(0, 3)]
let bindless: ParameterBlock<Bindless>;

export func getDescriptorFromHandle<T>(
  DescriptorHandle<T> handle) where T : IOpaqueDescriptor -> T
{
  // @TODO: needs validation
  // @TODO: automatic combined texture-sampler grab -- needs some metafunctions
  if (T.kind == DescriptorKind.Sampler)
    return bindless.samplers[uint2(handle).x].asOpaqueDescriptor<T>();
  else
    return bindless.textures[uint2(handle).x].asOpaqueDescriptor<T>();
}
