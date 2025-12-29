#include <vulkan/VlkRenderContext.h>
#include <vulkan/VlkDevice.h>
#include <vulkan/VlkSwapchain.h>
#include <vulkan/VlkShaderProgram.h>
#include <vulkan/VlkBuffer.h>

#include <stdexcept>

namespace PixelMachine {
	namespace GPU {
		
		extern VlkRenderContext *s_vlkRenderContextP;

		VlkRenderContext::VlkRenderContext(HWND windowHandle) {

			if (!sm_vlkDeviceP) {
				sm_vlkDeviceP = new VlkDevice();
			}

			VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
			surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			surfaceInfo.pNext = nullptr;
			surfaceInfo.hinstance = GetModuleHandle(NULL);
			surfaceInfo.hwnd = windowHandle;

			vkCreateWin32SurfaceKHR(sm_vlkDeviceP->GetVkInstance(), &surfaceInfo, nullptr, &m_vkWinSurface);

			if (!m_vkWinSurface) {
				throw new std::runtime_error("VlkRenderContext init fail - cannot create VkSurface.");
			}

			m_vkWinSurfaceFormat.format = VkFormat::VK_FORMAT_B8G8R8A8_SRGB;
			m_vkWinSurfaceFormat.colorSpace = VkColorSpaceKHR::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

			bool adapterNotFound = true;
			for (uint32_t i = 0; i < sm_vlkDeviceP->GetAdapterCount(); i++) {
				VlkAdapter adapter = sm_vlkDeviceP->GetAdapter(i);
				if (adapter.SurfaceFormatAvailable(m_vkWinSurface, m_vkWinSurfaceFormat) &&
					adapter.PresentModeAvailable(m_vkWinSurface, VK_PRESENT_MODE_FIFO_KHR)) {
					sm_vlkDeviceP->SetAdapter(i);
					adapterNotFound = false;
					break;
				}
			}

			if (adapterNotFound) {
				throw new std::runtime_error("VlkRenderContext init fail - cannot find suitable physical device.");
			}

			m_vlkSwapchainP = new VlkSwapchain(m_vkWinSurface, m_vkWinSurfaceFormat, VK_PRESENT_MODE_FIFO_KHR);

			VkDevice device = sm_vlkDeviceP->GetHandle();

			VkCommandPoolCreateInfo commandPoolInfo = {};
			commandPoolInfo.queueFamilyIndex = sm_vlkDeviceP->GetActiveQueue().second;
			commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			vkCreateCommandPool(device, &commandPoolInfo, nullptr, &m_vkCommandPool);

			VkCommandBufferAllocateInfo commandBufferInfo = {};
			commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			commandBufferInfo.commandBufferCount = 1;
			commandBufferInfo.commandPool = m_vkCommandPool;

			vkAllocateCommandBuffers(device, &commandBufferInfo, &m_vkCommandBuffer);

			VkSemaphoreCreateInfo semaphoreInfo = {};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			m_renderDone.resize(m_vlkSwapchainP->GetImagesCount());

			for (auto &sem : m_renderDone) {
				VkSemaphore semaphore = VK_NULL_HANDLE;
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
				sem = semaphore;
			}

			VkFenceCreateInfo fenceInfo = {};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			vkCreateFence(device, &fenceInfo, nullptr, &m_vkCmdCompletedFence);

		}

		VlkRenderContext::~VlkRenderContext() {

			m_vlkPasses.clear();

			VkDevice device = sm_vlkDeviceP->GetHandle();

			if (m_vkCmdCompletedFence) {
				vkDestroyFence(device, m_vkCmdCompletedFence, nullptr);
			}

			for (auto &sem : m_renderDone) {
				vkDestroySemaphore(device, sem, nullptr);
			}

			if (m_vkCommandBuffer) {
				vkFreeCommandBuffers(device, m_vkCommandPool, 1, &m_vkCommandBuffer);
			}

			if (m_vkCommandBuffer) {
				vkDestroyCommandPool(device, m_vkCommandPool, nullptr);
			}

			if (m_vlkSwapchainP) {
				delete m_vlkSwapchainP;
			}

			if (m_vkWinSurface) {
				vkDestroySurfaceKHR(sm_vlkDeviceP->GetVkInstance(), m_vkWinSurface, nullptr);
			}

			if (sm_vlkDeviceP) {
				delete sm_vlkDeviceP;
				sm_vlkDeviceP = nullptr;
			}
		}

		void VlkRenderContext::RunPass(const int index) {

			vkWaitForFences(sm_vlkDeviceP->GetHandle(), 1u, &m_vkCmdCompletedFence, VK_TRUE, UINT64_MAX);
			vkResetFences(sm_vlkDeviceP->GetHandle(), 1u, &m_vkCmdCompletedFence);
			vkResetCommandBuffer(m_vkCommandBuffer, 0);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0;
			beginInfo.pInheritanceInfo = nullptr;

			vkBeginCommandBuffer(m_vkCommandBuffer, &beginInfo);

			VlkPass &pass = m_vlkPasses.at(index);
			VkClearValue clearColor = { pass.m_clearColor[0], pass.m_clearColor[1], pass.m_clearColor[2], 1.0f };
			VkRect2D renderArea = {}; // Viewport = Render Area = Scissor Rectangle

			VkRenderPassBeginInfo renderPassBeginInfo = {};

			if (pass.m_renderToScreen) {
				m_frameIndex = m_vlkSwapchainP->GetImage(&renderPassBeginInfo.framebuffer);

				VlkAdapter activeAdapter = sm_vlkDeviceP->GetActiveAdapter();
				VkSurfaceCapabilitiesKHR surfaceCaps = activeAdapter.GetSurfaceInfo(m_vkWinSurface);
				renderArea.extent.width = surfaceCaps.currentExtent.width;
				renderArea.extent.height = surfaceCaps.currentExtent.height;
				renderArea.offset = { 0 };
			}
			//else - Set render area according to a target texture extents

			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = m_vlkSwapchainP->GetVkRenderPass();
			renderPassBeginInfo.renderArea = renderArea;
			renderPassBeginInfo.clearValueCount = 1u;
			renderPassBeginInfo.pClearValues = &clearColor;


			vkCmdBeginRenderPass(m_vkCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(m_vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.m_vkPipeline);

			std::vector<VkBuffer> vbs;
			const VlkBuffer *indexBuffer = nullptr;

			for (auto buffer : pass.m_buffers) {
				if (buffer->GetType() == BufferType::VertexBuffer) {
					vbs.push_back(buffer->GetHandle());
				}
				else if (buffer->GetType() == BufferType::IndexBuffer) {
					indexBuffer = buffer;
				}
			}

			if (vbs.size()) {
				std::vector<VkDeviceSize> offsets(vbs.size());
				vkCmdBindVertexBuffers(m_vkCommandBuffer, 0, vbs.size(), vbs.data(), offsets.data());
			}

			if (indexBuffer) {
				vkCmdBindIndexBuffer(m_vkCommandBuffer, indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
			}

			//vkCmdBindDescriptorSets

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = renderArea.extent.width;
			viewport.height = renderArea.extent.height;

			vkCmdSetViewportWithCount(m_vkCommandBuffer, 1u, &viewport);
			vkCmdSetScissorWithCount(m_vkCommandBuffer, 1u, &renderArea);

			if (indexBuffer) {
				vkCmdDrawIndexed(m_vkCommandBuffer, indexBuffer->GetSize() / indexBuffer->GetLayout().GetSize(), 1, 0, 0, 0);
			}
			else {
				vkCmdDraw(m_vkCommandBuffer, 3u, 1u, 0u, 0u);
			}

			vkCmdEndRenderPass(m_vkCommandBuffer);
			vkEndCommandBuffer(m_vkCommandBuffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore imageAvailable = m_vlkSwapchainP->GetImageAvailableSemaphore();
			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			submitInfo.waitSemaphoreCount = 1u;
			submitInfo.pWaitSemaphores = &imageAvailable;
			submitInfo.pWaitDstStageMask = waitStages;
			submitInfo.commandBufferCount = 1u;
			submitInfo.pCommandBuffers = &m_vkCommandBuffer;
			submitInfo.signalSemaphoreCount = 1u;
			submitInfo.pSignalSemaphores = &m_renderDone[m_frameIndex];

			vkQueueSubmit(sm_vlkDeviceP->GetActiveQueue().first, 1, &submitInfo, m_vkCmdCompletedFence);
		}

		void VlkRenderContext::PresentFrame() {

			VkPresentInfoKHR presentInfo{};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.waitSemaphoreCount = 1u;
			presentInfo.pWaitSemaphores = &m_renderDone[m_frameIndex];
			presentInfo.swapchainCount = 1u;
			VkSwapchainKHR swapchain = m_vlkSwapchainP->GetHandle();
			presentInfo.pSwapchains = &swapchain;
			presentInfo.pImageIndices = &m_frameIndex;

			vkQueuePresentKHR(sm_vlkDeviceP->GetActiveQueue().first, &presentInfo);
		}

		VkFormat GetVkFormat(BufferDataType shaderDataType) {
			switch (shaderDataType)
			{
			case BufferDataType::int1:		return VK_FORMAT_R32_SINT;
			case BufferDataType::int2:		return VK_FORMAT_R32G32_SINT;
			case BufferDataType::int3:		return VK_FORMAT_R32G32B32_SINT;
			case BufferDataType::int4:		return VK_FORMAT_R32G32B32A32_SINT;
			case BufferDataType::uint1:		return VK_FORMAT_R32_UINT;
			case BufferDataType::uint2:		return VK_FORMAT_R32G32_UINT;
			case BufferDataType::uint3:		return VK_FORMAT_R32G32B32_UINT;
			case BufferDataType::uint4:		return VK_FORMAT_R32G32B32A32_UINT;
			case BufferDataType::float1:	return VK_FORMAT_R32_SFLOAT;
			case BufferDataType::float2:	return VK_FORMAT_R32G32_SFLOAT;
			case BufferDataType::float3:	return VK_FORMAT_R32G32B32_SFLOAT;
			case BufferDataType::float4:	return VK_FORMAT_R32G32B32A32_SFLOAT;
			default: break;
			}
			return VK_FORMAT_UNDEFINED;
		}

		void VlkRenderContext::EndPass() {

			if (!m_vlkPasses.size())
				return;

			VlkPass &newPass = *m_vlkPasses.rbegin();

			std::vector<const VlkBuffer*> vbos;

			for (auto buffer : newPass.m_buffers) {
				if (buffer->GetType() == BufferType::VertexBuffer) {
					vbos.push_back(buffer);
				}
			}

			std::vector<VkVertexInputBindingDescription> vtxBindings(vbos.size());
			std::vector<VkVertexInputAttributeDescription> vtxAttributeDescs;
			uint32_t location = 0;

			for (uint32_t i = 0; i < vtxBindings.size(); i++) {

				vtxBindings[i].binding = i;
				vtxBindings[i].stride = vbos[i]->GetLayout().GetSize();
				vtxBindings[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				for (uint32_t j = 0; j < vbos[i]->GetLayout().GetAttributeCount(); j++) {

					VkVertexInputAttributeDescription vtxAttributeDesc = {};
					BufferAttribute attribute = vbos[i]->GetLayout().GetAttribute(j);
					vtxAttributeDesc.binding = i;
					vtxAttributeDesc.location = location;
					vtxAttributeDesc.offset = attribute.m_offset;
					vtxAttributeDesc.format = GetVkFormat(attribute.m_shaderDataType);
					vtxAttributeDescs.push_back(vtxAttributeDesc);
					location++;
				}

			}

			VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = {};
			vertexInputStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputStateInfo.vertexBindingDescriptionCount = vtxBindings.size();
			vertexInputStateInfo.pVertexBindingDescriptions = vtxBindings.data();
			vertexInputStateInfo.vertexAttributeDescriptionCount = vtxAttributeDescs.size();
			vertexInputStateInfo.pVertexAttributeDescriptions = vtxAttributeDescs.data();
	
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
			inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyInfo.topology = VkPrimitiveTopology(newPass.m_primitiveTopology);
			inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

			VkViewport viewport = {};
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			
			VkPipelineViewportStateCreateInfo viewportInfo = {};
			viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

			VkPipelineRasterizationStateCreateInfo rasterInfo = {};
			rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterInfo.depthClampEnable = VK_FALSE;
			rasterInfo.rasterizerDiscardEnable = VK_FALSE;
			rasterInfo.polygonMode = VK_POLYGON_MODE_FILL; // This should be set according to a primitive type
			rasterInfo.lineWidth = newPass.m_lineWidth;
			rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterInfo.depthBiasEnable = VK_FALSE;
			rasterInfo.depthBiasConstantFactor = 0.0f;
			rasterInfo.depthBiasClamp = 0.0f;
			rasterInfo.depthBiasSlopeFactor = 0.0f;

			VkPipelineMultisampleStateCreateInfo multisamplingInfo = {};
			multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisamplingInfo.sampleShadingEnable = VK_FALSE;
			multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			multisamplingInfo.minSampleShading = 1.0f;
			multisamplingInfo.pSampleMask = nullptr;
			multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
			multisamplingInfo.alphaToOneEnable = VK_FALSE;
			
			VkPipelineColorBlendAttachmentState colorBlendAttchState = {};
			colorBlendAttchState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttchState.blendEnable = VK_FALSE;
			colorBlendAttchState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttchState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttchState.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttchState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttchState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttchState.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
			colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlendInfo.logicOpEnable = VK_FALSE;
			colorBlendInfo.attachmentCount = 1;
			colorBlendInfo.pAttachments = &colorBlendAttchState;

			// newPass.CreateDescriptorSets();

			VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = newPass.m_shaderStagesInfo.size();
			pipelineInfo.pStages = newPass.m_shaderStagesInfo.data();
			pipelineInfo.pVertexInputState = &vertexInputStateInfo;
			pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
			pipelineInfo.pViewportState = &viewportInfo;
			pipelineInfo.pRasterizationState = &rasterInfo;
			pipelineInfo.pMultisampleState = &multisamplingInfo;
			pipelineInfo.pDepthStencilState = nullptr;
			pipelineInfo.pColorBlendState = &colorBlendInfo;
			pipelineInfo.renderPass = m_vlkSwapchainP->GetVkRenderPass();
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
			pipelineInfo.basePipelineIndex = -1;
			
			VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
			dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicStateInfo.dynamicStateCount = 3;
			VkDynamicState dynamicStates[3] = { 
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
				VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT
			};
			dynamicStateInfo.pDynamicStates = dynamicStates;
			pipelineInfo.pDynamicState = &dynamicStateInfo;
			
			VkDevice device = VlkRenderContext::GetVlkDevice()->GetHandle();

			vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &newPass.m_vkPipelineLayout);

			pipelineInfo.layout = newPass.m_vkPipelineLayout;

			vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &newPass.m_vkPipeline);

		}

		VlkRenderContext::VlkPass::~VlkPass() {

			VkDevice device = VlkRenderContext::GetVlkDevice()->GetHandle();

			vkDeviceWaitIdle(device);

			if (m_vkPipeline) {
				vkDestroyPipeline(device, m_vkPipeline, nullptr);
			}
			if (m_vkPipelineLayout) {
				vkDestroyPipelineLayout(device, m_vkPipelineLayout, nullptr);
			}

		}

		void VlkRenderContext::BindShaderProgram(const VlkShaderProgram *shaderProgram) {

			if (!shaderProgram || !m_vlkPasses.size()) {
				return;
			}

			VkPipelineShaderStageCreateInfo shaderInfo = {};
			shaderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderInfo.pName = "main";
			shaderInfo.module = shaderProgram->GetHandle();

			switch (shaderProgram->GetType())
			{
			case ShaderProgramType::VertexShader:
				shaderInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
				break;
			case ShaderProgramType::FragmentShader:
				shaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				break;
			case ShaderProgramType::ComputeShader:
				shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				break;
			default:
				break;
			}

			VlkPass &newPass = *m_vlkPasses.rbegin();
			newPass.m_shaderStagesInfo.push_back(shaderInfo);

		}

		void VlkRenderContext::BindBuffer(const VlkBuffer *buffer) {
			VlkPass &newPass = *m_vlkPasses.rbegin();
			newPass.m_buffers.push_back(buffer);
		}
	}
}