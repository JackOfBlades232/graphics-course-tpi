#include <etna/Etna.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/PipelineManager.hpp>

#include <memory>


// @TODO: refac

class HelloSlang
{
public:
  HelloSlang();

  void init();
  void execute();

private:
  etna::GlobalContext* context;

  std::unique_ptr<etna::OneShotCmdMgr> cmdMgr;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  std::uint32_t length;

  etna::ComputePipeline pipeline;

  etna::Buffer bufA;
  etna::Buffer bufB;
  etna::Buffer bufResult;

  void setup();
  void buildCommandBuffer(vk::CommandBuffer cmd_buf);
  void readback();
};

HelloSlang::HelloSlang()
  : length{16}
{
}

void HelloSlang::init()
{
  etna::initialize(
    etna::InitParams{
      .applicationName = "HelloSlang",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
    });

  context = &etna::get_context();

  cmdMgr = context->createOneShotCmdMgr();

  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = static_cast<std::uint32_t>(length * sizeof(float)),
    });
}

void HelloSlang::execute()
{
  setup();

  auto cmdBuf = cmdMgr->start();

  buildCommandBuffer(cmdBuf);

  cmdMgr->submitAndWait(std::move(cmdBuf));

  readback();
}

void HelloSlang::setup()
{
  etna::create_program(
    "hello_slang", {HELLO_SLANG_SLANG_SHADERS_ROOT "hello.slang-computeMain.spv"});

  bufA = context->createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(float) * length,
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "buffer0",
    });

  bufB = context->createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(float) * length,
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .name = "buffer1",
    });

  bufResult = context->createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(float) * length,
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
      .name = "result",
    });

  {
    std::vector<float> values(length);
    for (uint32_t i = 0; i < values.size(); ++i)
    {
      values[i] = (float)i;
    }
    transferHelper->uploadBuffer<float>(*cmdMgr, bufA, 0, values);
  }

  {
    std::vector<float> values(length);
    for (uint32_t i = 0; i < values.size(); ++i)
    {
      values[i] = static_cast<float>(i * i);
    }
    transferHelper->uploadBuffer<float>(*cmdMgr, bufB, 0, values);
  }

  pipeline = context->getPipelineManager().createComputePipeline("hello_slang", {});
}

void HelloSlang::buildCommandBuffer(vk::CommandBuffer cmd_buf)
{
  ETNA_CHECK_VK_RESULT(cmd_buf.begin(vk::CommandBufferBeginInfo{}));

  auto helloSlangInfo = etna::get_shader_program("hello_slang");

  auto set = etna::create_descriptor_set(
    helloSlangInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, bufA.genBinding()},
      etna::Binding{1, bufB.genBinding()},
      etna::Binding{2, bufResult.genBinding()},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getVkPipeline());
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch(1, 1, 1);

  ETNA_CHECK_VK_RESULT(cmd_buf.end());
}

void HelloSlang::readback()
{
  std::vector<float> values(length);
  transferHelper->readbackBuffer<float>(*cmdMgr, values, bufResult, 0);

  spdlog::info("Result on cpu:");
  for (float value : values)
    spdlog::info("  {}", value);
}

int main()
{
  {
    HelloSlang app;

    app.init();
    app.execute();
  }

  if (etna::is_initilized())
    etna::shutdown();

  return 0;
}
