#include "BlockRasterizer.h"
#include "TextureSampler.h"
#include <assert.h>
#include <iostream>

BlockRasterizer::BlockRasterizer()
	:primitiveType(PT_TRIANGLE_LIST), vs(0), ps(0), geomLayout(0){

	blendState.AlphaOperation = BLEND_OP_ADD;
	blendState.SrcAlpha = BLEND_SRC_ALPHA;
	blendState.DstAlpha = BLEND_INV_SRC_ALPHA;
	blendState.BlendEnable = false;

	NDCPlanes[0] = RasterizerPlane(0, 0, 0, 1);//WTF?
	NDCPlanes[1] = RasterizerPlane(1, 0, 0, 1);//left
	NDCPlanes[2] = RasterizerPlane(-1, 0, 0, 1);//right
	NDCPlanes[3] = RasterizerPlane(0, 1, 0, 1);//top
	NDCPlanes[4] = RasterizerPlane(0, -1, 0, 1);//bottom
	NDCPlanes[5] = RasterizerPlane(0, 0, 1, 0);//near plane (D3D NDC, NeraClip Z == 0)
	NDCPlanes[6] = RasterizerPlane(0, 0, -1, 1);//far
}

BlockRasterizer::~BlockRasterizer() {

}

bool BlockRasterizer::ClipToFrustumPlane(RasterizerPlane plane, ClipVector& src, ClipVector& dst) {
	auto face_count = src.size();
	bool clipped = false;
	for (size_t i = 0; i < face_count; i++) {
		//read face from input
		ClipFace face = src[i];
		int v0out, v1out, v2out, vout_cnt;
		v0out = v1out = v2out = vout_cnt = 0;
		if (lm::dot(plane, face.v0.reg[0]) <= 0) { v0out++; vout_cnt++; }
		if (lm::dot(plane, face.v1.reg[0]) <= 0) { v1out++; vout_cnt++; }
		if (lm::dot(plane, face.v2.reg[0]) <= 0) { v2out++; vout_cnt++; }


		switch (vout_cnt) {
		case 3://all 3 vertices behind plane! Clip entire triangle
			clipped = true;
			continue;
		case 2:
		{
			//printf("2 vertices behind plane!\n");
			clipped = true;
			RegisterBlock* vert[3];
			RegisterBlock vA, vB;
			if (v0out && v1out) { vert[0] = &face.v0; vert[1] = &face.v1; vert[2] = &face.v2; }
			if (v1out && v2out) { vert[0] = &face.v1; vert[1] = &face.v2; vert[2] = &face.v0; }
			if (v2out && v0out) { vert[0] = &face.v2; vert[1] = &face.v0; vert[2] = &face.v1; }

			float alpha_a = lm::dot(plane, vert[2]->reg[0]) / (lm::dot(plane, vert[2]->reg[0]) - lm::dot(plane, vert[1]->reg[0]));
			float alpha_b = lm::dot(plane, vert[2]->reg[0]) / (lm::dot(plane, vert[2]->reg[0]) - lm::dot(plane, vert[0]->reg[0]));

			for (size_t k = 0; k < geomLayout->Size(); k++) {
				vA.reg[k] = vert[2]->reg[k] + (vert[1]->reg[k] - vert[2]->reg[k]) * alpha_a;
				vB.reg[k] = vert[2]->reg[k] + (vert[0]->reg[k] - vert[2]->reg[k]) * alpha_b;
			}

			ClipFace fa;
			fa.v0 = vA;
			fa.v1 = *vert[2];
			fa.v2 = vB;
			dst.push_back(fa);

		}
		break;
		case 1:
		{
			clipped = true;
			RegisterBlock* vert[3];
			RegisterBlock vA, vB;
			if (v0out) { vert[0] = &face.v0; vert[1] = &face.v1; vert[2] = &face.v2; }
			if (v1out) { vert[0] = &face.v1; vert[1] = &face.v2; vert[2] = &face.v0; }
			if (v2out) { vert[0] = &face.v2; vert[1] = &face.v0; vert[2] = &face.v1; }

			float alpha_a = lm::dot(plane, vert[2]->reg[0]) / (lm::dot(plane, vert[2]->reg[0]) - lm::dot(plane, vert[0]->reg[0]));
			float alpha_b = lm::dot(plane, vert[1]->reg[0]) / (lm::dot(plane, vert[1]->reg[0]) - lm::dot(plane, vert[0]->reg[0]));

			for (size_t k = 0; k < geomLayout->Size(); k++) {
				vA.reg[k] = vert[2]->reg[k] + (vert[0]->reg[k] - vert[2]->reg[k]) * alpha_a;
				vB.reg[k] = vert[1]->reg[k] + (vert[0]->reg[k] - vert[1]->reg[k]) * alpha_b;
			}

			ClipFace fa;
			fa.v0 = vB;
			fa.v1 = *vert[1];
			fa.v2 = vA;
			dst.push_back(fa);

			ClipFace fb;
			fb.v0 = vA;
			fb.v1 = *vert[1];
			fb.v2 = *vert[2];
			dst.push_back(fb);

			//DrawTriangle(rA_out, *vert[1], *vert[2]);
		}
		break;
		case 0:
			dst.push_back(face); //nothing to crop! Just push it
			break;
		}
	}
	return clipped;
}

void BlockRasterizer::ClipToFrustum(ClipFace face, ClipVector& dst) {
	ClipVector cv1, cv2;

	dst.clear();

	cv1.push_back(face);

	if(ClipToFrustumPlane(NDCPlanes[0], cv1, cv2)){
		std::cout << "Clipped with plane 0" << std::endl;
	}
	cv1.clear();

	if (ClipToFrustumPlane(NDCPlanes[1], cv2, cv1)) {
		std::cout << "Clipped with plane 1" << std::endl;
	}
	cv2.clear();

	if (ClipToFrustumPlane(NDCPlanes[2], cv1, cv2)) {
		std::cout << "Clipped with plane 2" << std::endl;
	}
	cv1.clear();

	if (ClipToFrustumPlane(NDCPlanes[3], cv2, cv1)) {
		std::cout << "Clipped with plane 3" << std::endl;
	}
	cv2.clear();

	if (ClipToFrustumPlane(NDCPlanes[4], cv1, cv2)) {
		std::cout << "Clipped with plane 4" << std::endl;
	}
	cv1.clear();

	if (ClipToFrustumPlane(NDCPlanes[5], cv2, cv1)) {
		std::cout << "Clipped with plane 5" << std::endl;
	}
	if (ClipToFrustumPlane(NDCPlanes[6], cv1, dst)) {
		std::cout << "Clipped with plane 6" << std::endl;
	}
}

Texture2D* BlockRasterizer::GetBackBuffer() {
	return backBuffer;
}

Texture2D* BlockRasterizer::GetDepthBuffer() {
	return depthBuffer;
}

void BlockRasterizer::set_color_buffer(Texture2D* buffer) {
	backBuffer = buffer;
}

void BlockRasterizer::set_depth_buffer(Texture2D* buffer) {
	depthBuffer = buffer;
}

void BlockRasterizer::SetPrimitiveType(int type) {
	primitiveType = type;
}

void BlockRasterizer::Draw(size_t offset, size_t length) {
	if (!length) return;
	switch (primitiveType) {
	case PT_TRIANGLE_LIST:
	{
		auto faceCount = length / 3;
		Buffer* geom = vbSlots[0].buffer;

		if (!faceCount){
			DebugInfo("No faces to draw");
			return;
		}
		if (!geom){
			DebugError("Vertex Buffer is not set");
			return;
		}

		auto stride = vbSlots[0].stride;

		//Buffer is too small for this draw call
		if (geom->Size() / stride < faceCount * 3) {
			DebugError("[Draw] Vertex Buffer is too small!");
			return;
		}
		uint8_t* data = (uint8_t*)geom->GetPointer();
		for (size_t i = 0; i < faceCount; i++) {
			//setup vertex data pointers
			uint8_t* v0 = data + ((offset + 0 + i * 3) * stride);
			uint8_t* v1 = data + ((offset + 1 + i * 3) * stride);
			uint8_t* v2 = data + ((offset + 2 + i * 3) * stride);
			draw_impl(v0, v1, v2);
		}
	}
	break;
	}
}

void BlockRasterizer::DrawIndexed(size_t index_count, size_t start_index_location)
{
	if (index_count == 0) return;
	switch (primitiveType) {
	case PT_TRIANGLE_LIST:
	{
		int faceCount = index_count / 3;
		Buffer* geom = vbSlots[0].buffer;
		Buffer* indices = ibSlots[0];

		if (!faceCount) {
			DebugInfo("No faces to draw");
			return;
		}
		if (!geom) {
			DebugError("Vertex Buffer is not set");
			return;
		}
		if (!indices) {
			DebugError("Index Buffer is not set");
			return;
		}
		int stride = vbSlots[0].stride;

		uint8_t* vertex_data = (uint8_t*)geom->GetPointer();
		auto vertices_count = geom->Size() / vbSlots[0].stride;
		indices_t* index_data = (indices_t*)indices->GetPointer();

		for (size_t i = 0; i < index_count; i+=3) {
			//setup vertex data pointers
			auto id0 = index_data[i + 0 + start_index_location];
			auto id1 = index_data[i + 1 + start_index_location];
			auto id2 = index_data[i + 2 + start_index_location];

			uint8_t* v0 = vertex_data + id0 * stride;
			uint8_t* v1 = vertex_data + id1 * stride;
			uint8_t* v2 = vertex_data + id2 * stride;
			draw_impl(v0, v1, v2);
		}
	}
	break;
	}
}

void BlockRasterizer::draw_impl(void* v0, void* v1, void* v2)
{
	int elements = geomLayout->Size();
	for (int i = 0; i < elements; i++) { //TODO: OMG WTF! Fix memory corruption!!!
		InputElement* e = geomLayout->GetElement(i);
		int rd = regMapping[i];
		if(rd >= 0){
			r0_in[rd] = reinterpret_cast<float4*>((uint8_t*)v0 + e->Offset);
			r1_in[rd] = reinterpret_cast<float4*>((uint8_t*)v1 + e->Offset);
			r2_in[rd] = reinterpret_cast<float4*>((uint8_t*)v2 + e->Offset);
		}
	}
	vs->Execute(&r0_in[0], &r0_out.reg[0]);
	vs->Execute(&r1_in[0], &r1_out.reg[0]);
	int numInterpolators = vs->Execute(&r2_in[0], &r2_out.reg[0]);


	r0_out.reg[0].y = -r0_out.reg[0].y;
	r1_out.reg[0].y = -r1_out.reg[0].y;
	r2_out.reg[0].y = -r2_out.reg[0].y;

	ClipVector cv;
	ClipFace cf;
	cf.v0 = r0_out;
	cf.v1 = r1_out;
	cf.v2 = r2_out;

	ClipToFrustum(cf, cv);

	for (size_t l = 0; l < cv.size(); l++) {
		ClipFace cf_render = cv[l];
		DrawTriangle(cf_render.v0, cf_render.v1, cf_render.v2);
	}
}

void BlockRasterizer::FixupMapping() {
	if (geomLayout != 0) {
		if (vs != 0) {
			auto elements = geomLayout->Size();
			for (size_t i = 0; i < elements; i++) {
				regMapping[i] = vs->FindRegister(geomLayout->GetElement(i)->name);
			}
		}
	}
}

void BlockRasterizer::SetVertexBuffer(Buffer* vb, size_t slot, size_t stride) {
	vbSlots[slot].buffer = vb;
	vbSlots[slot].stride = stride;
}

void BlockRasterizer::SetIndexBuffer(Buffer* ib, size_t slot){
	ibSlots[slot] = ib;
}

void BlockRasterizer::SetInputLayout(IInputLayout* layout) {
	geomLayout = layout;
	FixupMapping();
}

void BlockRasterizer::SetPixelShader(PixelShader* shader) {
	ps = shader;
}

void BlockRasterizer::SetVertexShader(VertexShader* shader) {
	vs = shader;
	FixupMapping();
}

void BlockRasterizer::SetBlendState(BlendState* state) {
	blendState = *state;
}

float BlockRasterizer::GetBlendAlpha(BLEND_SOURCE src, const float4& srcColor, const float4& dstColor) {
	switch (src) {
	case BLEND_ZERO:
		return 0.0f;
	case BLEND_ONE:
		return 1.0f;
	case BLEND_SRC_ALPHA:
		return srcColor.w;
	case BLEND_INV_SRC_ALPHA:
		return 1.0f - srcColor.w;
	}
	return 0;
}

uint32_t BlockRasterizer::ConvertColor(const float4& color) {
	uint32_t r = (uint32_t)(color.x * 255.0f);
	uint32_t g = (uint32_t)(color.y * 255.0f);
	uint32_t b = (uint32_t)(color.z * 255.0f);
	uint32_t a = (uint32_t)(color.w * 255.0f);

	return (a << 24) | (r << 16) | (g << 8) | (b);
}

float4 BlockRasterizer::ConvertColor(uint32_t color) {
	float d = 1.0f / 255.0f;

	float a = (float)((color >> 24) & 0xff) * d;
	float r = (float)((color >> 16) & 0xff) * d;
	float g = (float)((color >> 8) & 0xff) * d;
	float b = (float)((color >> 0) & 0xff) * d;

	return float4(r, g, b, a);
}

void BlockRasterizer::DrawTriangle(RegisterBlock r0_src, RegisterBlock r1_src, RegisterBlock r2_src) {
	float sw_h = (float)backBuffer->width / 2.0f;
	float sh_h = (float)backBuffer->height / 2.0f;

	float scrW_h = (float)(backBuffer->width / 2);
	float scrH_h = (float)(backBuffer->height / 2);

	RegisterBlock* p0 = &r0_src;
	RegisterBlock* p1 = &r1_src;
	RegisterBlock* p2 = &r2_src;

	//p1->reg[0].W = abs(p1->reg[0].W);
	float iw0 = 1.0f / p0->reg[0].w;
	float iw1 = 1.0f / p1->reg[0].w;
	float iw2 = 1.0f / p2->reg[0].w;

	//transform vertices to viewport space
	p0->reg[0].x = (p0->reg[0].x * iw0) * scrW_h + scrW_h;
	p0->reg[0].y = (p0->reg[0].y * iw0) * scrH_h + scrH_h;
	p0->reg[0].z = (p0->reg[0].z * iw0);

	p1->reg[0].x = (p1->reg[0].x * iw1) * scrW_h + scrW_h;
	p1->reg[0].y = (p1->reg[0].y * iw1) * scrH_h + scrH_h;
	p1->reg[0].z = (p1->reg[0].z * iw1);

	p2->reg[0].x = (p2->reg[0].x * iw2) * scrW_h + scrW_h;
	p2->reg[0].y = (p2->reg[0].y * iw2) * scrH_h + scrH_h;
	p2->reg[0].z = (p2->reg[0].z * iw2);

	float3 camZ(p0->reg[0].z, p1->reg[0].z, p2->reg[0].z);

	bool disableCulling = false;
	if (disableCulling)
		if (((p1->reg[0].x - p0->reg[0].x) * (p2->reg[0].y - p0->reg[0].y) - (p1->reg[0].y- p0->reg[0].y) * (p2->reg[0].x - p0->reg[0].x) <= 0)) {
			RegisterBlock* pTmp = p0;
			p0 = p2;
			p2 = pTmp;
		}

	//interpolation weights per vertex
	float3 col1(1.0f, 0.0f, 0.0f);
	float3 col2(0.0f, 1.0f, 0.0f);
	float3 col3(0.0f, 0.0f, 1.0f);

	//weights deltas
	float3 dc31 = col3 - col1;
	float3 dc21 = col2 - col1;

	//temporary register for perspective correction value
	float pc;


	const int x1 = (int)std::round(16.0f * p0->reg[0].x);
	const int x2 = (int)std::round(16.0f * p1->reg[0].x);
	const int x3 = (int)std::round(16.0f * p2->reg[0].x);
	const int y1 = (int)std::round(16.0f * p0->reg[0].y);
	const int y2 = (int)std::round(16.0f * p1->reg[0].y);
	const int y3 = (int)std::round(16.0f * p2->reg[0].y);

	const int Dx12 = x1 - x2;
	const int Dx23 = x2 - x3;
	const int Dx31 = x3 - x1;

	const int Dy12 = y1 - y2;
	const int Dy23 = y2 - y3;
	const int Dy31 = y3 - y1;

	const int FDx12 = Dx12 << 4;
	const int FDx23 = Dx23 << 4;
	const int FDx31 = Dx31 << 4;

	const int FDy12 = Dy12 << 4;
	const int FDy23 = Dy23 << 4;
	const int FDy31 = Dy31 << 4;

	//bounding rectangle
	int minX = ((std::min)((std::min)(x1, x2), x3) + 0xF) >> 4;
	int maxX = ((std::max)((std::max)(x1, x2), x3) + 0xF) >> 4;
	int minY = ((std::min)((std::min)(y1, y2), y3) + 0xF) >> 4;
	int maxY = ((std::max)((std::max)(y1, y2), y3) + 0xF) >> 4;

	const int ix1 = (x1 + 0xf) >> 4;
	const int ix2 = (x2 + 0xf) >> 4;
	const int ix3 = (x3 + 0xf) >> 4;

	const int iy1 = (y1 + 0xf) >> 4;
	const int iy2 = (y2 + 0xf) >> 4;
	const int iy3 = (y3 + 0xf) >> 4;

	float3 A = dc31 * ((iy2 - iy1)) - dc21 * ((iy3 - iy1));
	float3 B = dc21 * ((ix3 - ix1)) - dc31 * ((ix2 - ix1));
	int C = ((iy3 - iy1)) * ((ix2 - ix1)) - ((ix3 - ix1)) * ((iy2 - iy1));

	if (C == 0) return;

	float3 dzdx = A / (float)-C;
	float3 dzdy = B / (float)-C;

	if (maxY < 0)
		return;
	if (minY >= (int)backBuffer->height)
		return;
	if (maxX < 0)
		return;
	if (minX >= (int)backBuffer->width)
		return;

	if (minY < 0)
		minY = 0;
	if (maxY >= (int)backBuffer->height)
		maxY = (int)backBuffer->height - 1;

	if (minX < 0)
		minX = 0;
	if (maxX >= (int)backBuffer->width)
		maxX = (int)backBuffer->width - 1;


	int blockSize = 8;

	minX &= ~(blockSize - 1);
	minY &= ~(blockSize - 1);

	uint32_t* bbPtr = reinterpret_cast<uint32_t*>(backBuffer->getBuffer()->GetPointer());

	float* dbPtr = reinterpret_cast<float*>(depthBuffer->getBuffer()->GetPointer());

	bbPtr += minY * backBuffer->width;
	dbPtr += minY * depthBuffer->width;

	int C1 = Dy12*x1 - Dx12*y1;
	int C2 = Dy23*x2 - Dx23*y2;
	int C3 = Dy31*x3 - Dx31*y3;


	//top-left fill convention fix
	/*if (Dy12 > 0 || (Dy12 == 0 && Dx12 > 0)) C1--;
	if (Dy23 > 0 || (Dy23 == 0 && Dx23 > 0)) C2--;
	if (Dy31 > 0 || (Dy31 == 0 && Dx31 > 0)) C3--;*/
	if (!(Dy12 < 0 || (Dy12 == 0 && Dx12 > 0))) C1--;
	if (!(Dy23 < 0 || (Dy23 == 0 && Dx23 > 0))) C2--;
	if (!(Dy31 < 0 || (Dy31 == 0 && Dx31 > 0))) C3--;

	//scan all blocks
	for (int y = minY; y < maxY; y += blockSize) {
		for (int x = minX; x < maxX; x += blockSize) {
			//find block corners
			int x0 = x << 4;
			int x1 = (x + blockSize - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + blockSize - 1) << 4;

			//evaluate half space functions for each corner
			bool a00 = C1 + Dx12 * y0 - Dy12 * x0 < 0;
			bool a01 = C1 + Dx12 * y0 - Dy12 * x1 < 0;
			bool a10 = C1 + Dx12 * y1 - Dy12 * x0 < 0;
			bool a11 = C1 + Dx12 * y1 - Dy12 * x1 < 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + Dx23 * y0 - Dy23 * x0 < 0;
			bool b01 = C2 + Dx23 * y0 - Dy23 * x1 < 0;
			bool b10 = C2 + Dx23 * y1 - Dy23 * x0 < 0;
			bool b11 = C2 + Dx23 * y1 - Dy23 * x1 < 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + Dx31 * y0 - Dy31 * x0 < 0;
			bool c01 = C3 + Dx31 * y0 - Dy31 * x1 < 0;
			bool c10 = C3 + Dx31 * y1 - Dy31 * x0 < 0;
			bool c11 = C3 + Dx31 * y1 - Dy31 * x1 < 0;
			int c = ((uint32_t)c00 << 0) | ((uint32_t)c10 << 1) | ((uint32_t)c01 << 2) | ((uint32_t)c11 << 3);

			//Skip entire block if all it's 4 corners outside of triangle edges
			if (a == 0 || b == 0 || c == 0)
				continue;

			uint32_t* colorBuffer = bbPtr;
			float* zBuffer = dbPtr;

			//Whole block is inside triangle!
			if (a == 0xf && b == 0xf && c == 0xf) {
				for (int by = 0; by < blockSize; by++) {
					for (int bx = x; bx < (x + blockSize); bx++) {
						//compute interpolation weights
						float3 interpolators = col1 + dzdx * (bx - ix1) + dzdy*(y + by - iy1);
						float3 invW(1.0f / p0->reg[0].w * interpolators.x, 1.0f / p1->reg[0].w * interpolators.y, 1.0f / p2->reg[0].w * interpolators.z);

						//compute 1/w value for perspective correction
						pc = 1.0f / (invW.x + invW.y + invW.z);

						//interpolate Z
						float z_interp = p0->reg[0].z * invW.x + p1->reg[0].z * invW.y + p2->reg[0].z * invW.z;
						z_interp *= pc;

						if(zBuffer[bx] > z_interp)
						{
							zBuffer[bx] = z_interp;
							//interpolate registers
							for (int r = 1; r < REG_COUNT; r++) {
								r_ps.reg[r] = p0->reg[r] * invW.x + p1->reg[r] * invW.y + p2->reg[r] * invW.z;
								r_ps.reg[r] *= pc; //add perspective correction
							}

							float4 pixel_color = ps->Execute(&r_ps.reg[0]);

							if (blendState.BlendEnable) {
								uint32_t dc = colorBuffer[bx];

								float4 dstColor = ConvertColor(dc);

								//get source alpha
								float srcAlpha = GetBlendAlpha(blendState.SrcAlpha, pixel_color, dstColor);
								float dstAlpha = GetBlendAlpha(blendState.DstAlpha, pixel_color, dstColor);

								pixel_color = pixel_color * srcAlpha + dstColor * dstAlpha;
							}
							//colorBuffer[bx] += 0x00606060;
							colorBuffer[bx] =  ConvertColor(pixel_color);
						}
					}
					colorBuffer += backBuffer->width;
					zBuffer += depthBuffer->width;
				}
			} else //Partially covered block
			{
				int CY1 = C1 + Dx12 * y0 - Dy12 * x0;
				int CY2 = C2 + Dx23 * y0 - Dy23 * x0;
				int CY3 = C3 + Dx31 * y0 - Dy31 * x0;

				for (int by = y; by < (y + blockSize); by++) {
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int bx = x; bx < (x + blockSize); bx++) {
						if (CX1 < 0 && CX2 < 0 && CX3 < 0) {
							float3 interpolators = col1 + dzdx*(bx - ix1) + dzdy*(by - iy1);

							float3 invW(1.0f / p0->reg[0].w * interpolators.x, 1.0f / p1->reg[0].w * interpolators.y, 1.0f / p2->reg[0].w * interpolators.z);

							//compute 1/w value for perspective correction
							pc = 1.0f / (invW.x + invW.y + invW.z);

							//interpolate Z
							float z_interp = p0->reg[0].z * invW.x + p1->reg[0].z * invW.y + p2->reg[0].z * invW.z;
							z_interp *= pc;


							if (zBuffer[bx] > z_interp) {
								zBuffer[bx] = z_interp;
								//interpolate registers
								for (int r = 1; r < REG_COUNT; r++) {
									r_ps.reg[r] = p0->reg[r] * invW.x + p1->reg[r] * invW.y + p2->reg[r] * invW.z;
									r_ps.reg[r] *= pc; //add perspective correction
								}

								float4 rst;
								rst = ps->Execute(&r_ps.reg[0]);

								//UINT texcolor = ConvertColor(rst);

								if (blendState.BlendEnable) {
									uint32_t dc = colorBuffer[bx];

									float4 dstColor = ConvertColor(dc);

									float srcAlpha;
									float dstAlpha;

									//get source alpha
									srcAlpha = GetBlendAlpha(blendState.SrcAlpha, rst, dstColor);
									dstAlpha = GetBlendAlpha(blendState.DstAlpha, rst, dstColor);

									rst = rst * srcAlpha + dstColor * dstAlpha;
								}
								//colorBuffer[bx] += 0x00606060;
								colorBuffer[bx] = ConvertColor(rst);
							}
						}

						CX1 -= FDy12;
						CX2 -= FDy23;
						CX3 -= FDy31;
					}
					CY1 += FDx12;
					CY2 += FDx23;
					CY3 += FDx31;

					colorBuffer += backBuffer->width;
					zBuffer += depthBuffer->width;
				}
			}
		}
		bbPtr += backBuffer->width * blockSize;
		dbPtr += depthBuffer->width * blockSize;
	}
}
