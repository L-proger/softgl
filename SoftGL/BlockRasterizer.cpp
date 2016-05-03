#include "BlockRasterizer.h"
#include "TextureSampler.h"
#include <assert.h>

BlockRasterizer::BlockRasterizer()
    :primitiveType(PT_TRIANGLE_LIST), vs(0), ps(0), geomLayout(0), render_window(wnd),
      alpha_test_enable(false), alpha_ref(0.5f), ambient_color(0,0,0), ambient_enable(false){

    Mat4x4Identity(&mWorld);
    Mat4x4Identity(&mView);
    Mat4x4Identity(&mProj);

    memset(&ffp_state, 0, sizeof(ffp_state));

    FixupMapping();

    blendState.AlphaOperation = BLEND_OP_ADD;
    blendState.SrcAlpha = BLEND_SRC_ALPHA;
    blendState.DstAlpha = BLEND_INV_SRC_ALPHA;
    blendState.BlendEnable = false;

   /* scrW_h = (float)(scrW / 2);
    scrH_h = (float)(scrH / 2);*/

    NDCPlanes[0].Set(0,0,0,1);
    NDCPlanes[1].Set(1,0,0,1);
    NDCPlanes[2].Set(-1,0,0,1);
    NDCPlanes[3].Set(0,1,0,1);
    NDCPlanes[4].Set(0,-1,0,1);
    NDCPlanes[5].Set(0,0,1,1);
    NDCPlanes[6].Set(0,0,-1,1);

    for(int i = 0; i < MAX_TEX_SLOTS; i++)
        tex_slots[i] = 0;
}

BlockRasterizer::~BlockRasterizer(){

}

void BlockRasterizer::SetWorldMatrix(const Matrix4x4& m){
    mWorld = m;
}

void BlockRasterizer::SetViewMatrix(const Matrix4x4& m){
    mView = m;
}

void BlockRasterizer::SetProjectionMatrix(const Matrix4x4& m)
{
    mProj = m;
}

int BlockRasterizer::GetPointNDCZone(const Vector4D& point){
    int flags = 0;
    if(point.W < 0.0f) flags |= (1 << ZONE_ZERO);
    if(point.X < -point.W) flags |= (1 << ZONE_LEFT);
    if(point.X > point.W) flags |= (1 << ZONE_RIGHT);
    if(point.Y < -point.W) flags |= (1 << ZONE_BOTTOM);
    if(point.Y > point.W) flags |= (1 << ZONE_TOP);
    if(point.Z < -point.W) flags |= (1 << ZONE_FAR);
    if(point.Z > point.W) flags |= (1 << ZONE_NEAR);
    return flags;
}

void BlockRasterizer::ClipToFrustumPlane(Plane plane, ClipVector* src, ClipVector* dst)
{
    int face_count = src->size();

    for(int i = 0; i < face_count; i++)
    {
        //read face from input
        ClipFace face = (*src)[i];
        int v0out, v1out, v2out, vout_cnt;
        v0out= v1out= v2out= vout_cnt = 0;
        if(PlaneDot(plane, face.v0.reg[0]) <=0) {v0out++; vout_cnt++;}
        if(PlaneDot(plane, face.v1.reg[0]) <=0) {v1out++; vout_cnt++;}
        if(PlaneDot(plane, face.v2.reg[0]) <=0) {v2out++; vout_cnt++;}


        switch(vout_cnt)
        {
        case 3://all 3 vertices behind near plane! BURN IT WITH FIRE!!!! ARRRRR!
            continue;
            break;
        case 2:
            {
            //printf("2 vertices behind plane!\n");

            RegisterBlock* vert[3];
            RegisterBlock vA, vB;
            if(v0out && v1out){vert[0] = &face.v0; vert[1] = &face.v1; vert[2] = &face.v2;}
            if(v1out && v2out){vert[0] = &face.v1; vert[1] = &face.v2; vert[2] = &face.v0;}
            if(v2out && v0out){vert[0] = &face.v2; vert[1] = &face.v0; vert[2] = &face.v1;}

            float alpha_a = PlaneDot(plane, vert[2]->reg[0]) / (PlaneDot(plane, vert[2]->reg[0]) - PlaneDot(plane, vert[1]->reg[0]));
            float alpha_b = PlaneDot(plane, vert[2]->reg[0]) / (PlaneDot(plane, vert[2]->reg[0]) - PlaneDot(plane, vert[0]->reg[0]));

            for(int k = 0; k < geomLayout->RegCount; k++){
                vA.reg[k] = vert[2]->reg[k] + (vert[1]->reg[k] - vert[2]->reg[k]) * alpha_a;
                vB.reg[k] = vert[2]->reg[k] + (vert[0]->reg[k] - vert[2]->reg[k]) * alpha_b;
            }

            ClipFace fa;
            fa.v0 = vA;
            fa.v1 = *vert[2];
            fa.v2 = vB;
            dst->push_back(fa);

            }
            break;
        case 1:
            {
                RegisterBlock* vert[3];
                RegisterBlock vA, vB;
                if(v0out){vert[0] = &face.v0; vert[1] = &face.v1; vert[2] = &face.v2;}
                if(v1out){vert[0] = &face.v1; vert[1] = &face.v2; vert[2] = &face.v0;}
                if(v2out){vert[0] = &face.v2; vert[1] = &face.v0; vert[2] = &face.v1;}

                float alpha_a = PlaneDot(plane, vert[2]->reg[0]) / (PlaneDot(plane, vert[2]->reg[0]) - PlaneDot(plane, vert[0]->reg[0]));
                float alpha_b = PlaneDot(plane, vert[1]->reg[0]) / (PlaneDot(plane, vert[1]->reg[0]) - PlaneDot(plane, vert[0]->reg[0]));



                for(int k = 0; k < geomLayout->RegCount; k++){
                    vA.reg[k] = vert[2]->reg[k] + (vert[0]->reg[k] - vert[2]->reg[k]) * alpha_a;
                    vB.reg[k] = vert[1]->reg[k] + (vert[0]->reg[k] - vert[1]->reg[k]) * alpha_b;
                }

                ClipFace fa;
                fa.v0 = vB;
                fa.v1 = *vert[1];
                fa.v2 = vA;
                dst->push_back(fa);

                ClipFace fb;
                fb.v0 = vA;
                fb.v1 = *vert[1];
                fb.v2 = *vert[2];
                dst->push_back(fb);

                //DrawTriangle(rA_out, *vert[1], *vert[2]);
            }
            break;
        case 0:
            dst->push_back(face); //nothing to crop! Just push it
            break;
        }
    }
}

void BlockRasterizer::ClipToFrustum(ClipFace face, ClipVector* dst)
{
    ClipVector cv1, cv2;

    dst->clear();

    cv1.push_back(face);

    ClipToFrustumPlane(NDCPlanes[0], &cv1, &cv2);
    cv1.clear();

    ClipToFrustumPlane(NDCPlanes[1], &cv2, &cv1);
    cv2.clear();

    ClipToFrustumPlane(NDCPlanes[2], &cv1, &cv2);
    cv1.clear();

    ClipToFrustumPlane(NDCPlanes[3], &cv2, &cv1);
    cv2.clear();

    ClipToFrustumPlane(NDCPlanes[4], &cv1, &cv2);
    cv1.clear();

    ClipToFrustumPlane(NDCPlanes[5], &cv2, &cv1);
    ClipToFrustumPlane(NDCPlanes[6], &cv1, dst);
}

void BlockRasterizer::SetFog(const FFPFog& fog)
{
    ffp_state.fog = fog;
    if(fog.start < 0.0f || fog.end < fog.start)
        ffp_state.fog.enabled = false;
}

Texture2D* BlockRasterizer::GetBackBuffer()
{
    return backBuffer;
}

Texture2D* BlockRasterizer::GetDepthBuffer()
{
    return depthBuffer;
}

void BlockRasterizer::Clear(int color)
{

    int64_t c;
    int* ptr = reinterpret_cast<int*>(&c);
    ptr[0] = ptr[1] = 0xff0000ff;


    int bbPixCnt = backBuffer->width * backBuffer->height;

    int quarterSize = bbPixCnt >> 2;
    int tail = bbPixCnt - (quarterSize << 2);



    int* bbPtr = reinterpret_cast<int*>(backBuffer->getBuffer()->getDataPtr());


    bbPtr[0] = color;
    for(int i = 1; i < bbPixCnt; i++)
        bbPtr[i] = bbPtr[i - 1];

    int dbPixCnt = depthBuffer->width * depthBuffer->height;

    float dbVal = 1.0f;



    float* dbPtr = reinterpret_cast<float*>(depthBuffer->getBuffer()->getDataPtr());

    for(int i = 0; i < dbPixCnt; i++)
        dbPtr[i] = dbVal;
}

void BlockRasterizer::Present(){
    render_window->Present(backBuffer);
}

void BlockRasterizer::SetPrimitiveType(int type){
    primitiveType = type;
}

void BlockRasterizer::Draw(int offset, int length)
{
    if(!length) return;
    switch(primitiveType)
    {
        case PT_TRIANGLE_LIST:
        {
            int faceCount = length / 3;
            VertexBuffer* geom  = vbSlots[0];

            if(!faceCount) return;
            if(!geom) return;

            int stride = geom->Stride();

            //buffer is too small for this draw call
            if(geom->Size() / stride < faceCount * 3)
            {
                printf("[Draw] Vertex buffer is too small!\n");
                return;
            }

            for(int i = 0; i < faceCount; i++)
            {
                char* data = (char*)geom->getDataPtr();
                //setup vertex data pointers
                char* v0 = data + ((offset + 0 + i * 3) * stride);
                char* v1 = data + ((offset + 1 + i * 3) * stride);
                char* v2 = data + ((offset + 2 + i * 3) * stride);



//apply FFP transform
#ifdef RENDER_FFP
                //setup register data pointers
                int elements = geomLayout->RegCount;
                for(int i = 0; i < elements; i++)
                {
                    InputElement* e = geomLayout->RegInfo + i;
                    r0_out.reg[i] = *reinterpret_cast<Vector4D*>(v0 + e->Offset);
                    r1_out.reg[i] = *reinterpret_cast<Vector4D*>(v1 + e->Offset);
                    r2_out.reg[i] = *reinterpret_cast<Vector4D*>(v2 + e->Offset);
                }

                Vector4D v0coord = r0_out.reg[0];
                v0coord.W = 1.0f;

                Vector4D v1coord = r1_out.reg[0];
                v1coord.W = 1.0f;

                Vector4D v2coord = r2_out.reg[0];
                v2coord.W = 1.0f;

                mWVP = mWorld * mView * mProj;
                Vector4D position;

                position = r0_out.reg[0];
                position.W = 1.0f;
                Vec4Transform(position, mWVP);
                r0_out.reg[0] = position;

                position = r1_out.reg[0];
                position.W = 1.0f;
                Vec4Transform(position, mWVP);
                r1_out.reg[0] = position;

                position = r2_out.reg[0];
                position.W = 1.0f;
                Vec4Transform(position, mWVP);
                r2_out.reg[0] = position;
#else

                int elements = geomLayout->RegCount;
                for(int i = 0; i < elements; i++)
                {
                    InputElement* e = geomLayout->RegInfo + i;
                    int rd = regMapping[i];
                    r0_in[rd] = reinterpret_cast<Vector4D*>(v0 + e->Offset);
                    r1_in[rd] = reinterpret_cast<Vector4D*>(v1 + e->Offset);
                    r2_in[rd] = reinterpret_cast<Vector4D*>(v2 + e->Offset);
                }
                vs->Execute(r0_in, &r0_out.reg[0]);
                vs->Execute(r1_in, &r1_out.reg[0]);
                int numInterpolators = vs->Execute(r2_in, &r2_out.reg[0]);
#endif

                ClipVector cv;
                ClipFace cf;
                cf.v0 = r0_out;
                cf.v1 = r1_out;
                cf.v2 = r2_out;

                ClipToFrustum(cf, &cv);

                for(int l = 0; l < cv.size(); l++){
                    ClipFace cf_render = cv[l];
                    DrawTriangle(cf_render.v0, cf_render.v1, cf_render.v2);
                }
            }
        }
        break;
    }
}

void BlockRasterizer::FixupMapping()
{
    if(geomLayout != 0)
    {
#ifdef RENDER_FFP
        regMapping[0] = geomLayout->FindElement(FFP_VERTEXELEMENT_POSITION);
        regMapping[1] = geomLayout->FindElement(FFP_VERTEXELEMENT_TEXCOORD);
        regMapping[2] = geomLayout->FindElement(FFP_VERTEXELEMENT_NORMAL);
        regMapping[3] = geomLayout->FindElement(FFP_VERTEXELEMENT_VERTEXCOLOR);
#else
        if(vs != 0)
        {
            int elements = geomLayout->RegCount;
            for(int i = 0; i < elements; i++)
            {
                regMapping[i] = vs->FindRegister(geomLayout->RegInfo[i].Name);
            }
        }
#endif
    }
}


void BlockRasterizer::SetVertexBuffer(VertexBuffer* vb, int slot)
{
    vbSlots[slot] = vb;
}

void BlockRasterizer::SetInputLayout(InputLayout* layout)
{
    geomLayout = layout;
    FixupMapping();
}

void BlockRasterizer::SetPixelShader(PixelShader* shader)
{
#ifdef RENDER_FFP
    assert(false && "FFP BUILD!");
#else
    ps = shader;
#endif
}

void BlockRasterizer::SetVertexShader(VertexShader* shader)
{
#ifdef RENDER_FFP
    assert(false && "FFP BUILD!");
#else
    vs = shader;
    FixupMapping();
#endif
}

void BlockRasterizer::SetBlendState(BlendState* state)
{
    blendState = *state;
}

float BlockRasterizer::GetBlendAlpha(BLEND_SOURCE src, const Vector4D& srcColor, const Vector4D& dstColor)
{
    switch(src)
    {
    case BLEND_ZERO:
        return 0.0f;
    case BLEND_ONE:
        return 1.0f;
    case BLEND_SRC_ALPHA:
        return srcColor.W;
    case BLEND_INV_SRC_ALPHA:
        return 1.0f - srcColor.W;
    }
	return 0;
}

uint32 BlockRasterizer::ConvertColor(const Vector4D& color)
{
    uint32 r = (uint32)(color.X * 255.0f);
    uint32 g = (uint32)(color.Y * 255.0f);
    uint32 b = (uint32)(color.Z * 255.0f);
    uint32 a = (uint32)(color.W * 255.0f);

    return (a << 24) | (r << 16) | (g << 8) | (b);
}

Vector4D BlockRasterizer::ConvertColor(uint32 color)
{
    float d = 1.0f / 255.0f;

    float a = (float)((color >> 24) & 0xff) * d;
    float r = (float)((color >> 16) & 0xff) * d;
    float g = (float)((color >> 8) & 0xff) * d;
    float b = (float)((color >> 0) & 0xff) * d;

    return Vector4D(r,g,b,a);
}

Vector4D BlockRasterizer::FFP_GetPixelColor(Vector4D* input, float z)
{
    Vector4D out_color;

    out_color = Vector4D(1.0f,1.0f,1.0f,1.0f);

    //apply texture
    if(tex_slots[0] != 0)
    {
        Vector4D tc = input[regMapping[1]];
        out_color = ConvertColor(sample2D(tex_slots[0], tc.X, tc.Y));
    }

    //apply ambient
    if(ambient_enable)
    {
        out_color.X += ambient_color.X;
        out_color.Y += ambient_color.Y;
        out_color.Z += ambient_color.Z;
    }


    //apply fog
    if(ffp_state.fog.enabled){
        if(z >= ffp_state.fog.start)
        {
            if(z <= ffp_state.fog.end) //point inside interpolation area
            {
                float fog_alpha = (z - ffp_state.fog.start) / (ffp_state.fog.end - ffp_state.fog.start);
                out_color.X = out_color.X * (1.0f - fog_alpha) + ffp_state.fog.color.X * fog_alpha;
                out_color.Y = out_color.Y * (1.0f - fog_alpha) + ffp_state.fog.color.Y * fog_alpha;
                out_color.Z = out_color.Z * (1.0f - fog_alpha) + ffp_state.fog.color.Z * fog_alpha;
            }
            else //full fog
            {
                out_color.X = ffp_state.fog.color.X;
                out_color.Y = ffp_state.fog.color.Y;
                out_color.Z = ffp_state.fog.color.Z;
            }
        }
    }

    return out_color;
}

void BlockRasterizer::SetTexture(Texture2D* tex, uint8 slot)
{
    assert(slot >= 0 && slot < MAX_TEX_SLOTS && "Invalid slot ID");
    tex_slots[slot] = tex;
}

void BlockRasterizer::DrawTriangle(RegisterBlock r0_src, RegisterBlock r1_src, RegisterBlock r2_src)
{
    float sw_h = (float)backBuffer->width / 2.0f;
    float sh_h = (float)backBuffer->height / 2.0f;

    RegisterBlock* p0 = &r0_src;
    RegisterBlock* p1 = &r1_src;
    RegisterBlock* p2 = &r2_src;

    //printf("W1: %f, W2: %f, W3: %f\n",p0->reg[0].W,p1->reg[0].W,p2->reg[0].W);


    //p1->reg[0].W = abs(p1->reg[0].W);
    float iw0 = 1.0f / p0->reg[0].W;
    float iw1 = 1.0f / p1->reg[0].W;
    float iw2 = 1.0f / p2->reg[0].W;

    //save camera space z-coord for future computations (fog, etc)
    Vector3D camZ(p0->reg[0].Z, p1->reg[0].Z, p2->reg[0].Z);

    //transform vertices to viewport space
    p0->reg[0].X = (p0->reg[0].X * iw0) * scrW_h + scrW_h;
    p0->reg[0].Y = (p0->reg[0].Y * iw0) * scrH_h + scrH_h;
    p0->reg[0].Z = (p0->reg[0].Z * iw0);

    p1->reg[0].X = (p1->reg[0].X * iw1) * scrW_h + scrW_h;
    p1->reg[0].Y = (p1->reg[0].Y * iw1) * scrH_h + scrH_h;
    p1->reg[0].Z = (p1->reg[0].Z * iw1);

    p2->reg[0].X = (p2->reg[0].X * iw2) * scrW_h + scrW_h;
    p2->reg[0].Y = (p2->reg[0].Y * iw2) * scrH_h + scrH_h;
    p2->reg[0].Z = (p2->reg[0].Z * iw2);


    bool disableCulling = false;
    if(disableCulling)
    if(((p1->reg[0].X - p0->reg[0].X) * (p2->reg[0].Y - p0->reg[0].Y) - (p1->reg[0].Y - p0->reg[0].Y) * (p2->reg[0].X - p0->reg[0].X) <= 0))
    {
        RegisterBlock* pTmp = p0;
        p0 = p2;
        p2 = pTmp;
    }

    //interpolation weights per vertex
    Vector3D col1(1.0f, 0.0f, 0.0f);
    Vector3D col2(0.0f, 1.0f, 0.0f);
    Vector3D col3(0.0f, 0.0f, 1.0f);

    //weights deltas
    Vector3D dc31 = col3 - col1;
    Vector3D dc21 = col2 - col1;

    //temporary register for perspective correction value
    float pc;


    const int x1 = fround(16.0f * p0->reg[0].X);
    const int x2 = fround(16.0f * p1->reg[0].X);
    const int x3 = fround(16.0f * p2->reg[0].X);

    const int y1 = fround(16.0f * p0->reg[0].Y);
    const int y2 = fround(16.0f * p1->reg[0].Y);
    const int y3 = fround(16.0f * p2->reg[0].Y);

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
    int minX = (min_i(x1, x2, x3) + 0xF) >> 4;
    int maxX = (max_i(x1, x2, x3) + 0xF) >> 4;
    int minY = (min_i(y1, y2, y3) + 0xF) >> 4;
    int maxY = (max_i(y1, y2, y3) + 0xF) >> 4;





    const int ix1 = (x1 + 0xf) >> 4;
    const int ix2 = (x2 + 0xf) >> 4;
    const int ix3 = (x3 + 0xf) >> 4;

    const int iy1 = (y1 + 0xf) >> 4;
    const int iy2 = (y2 + 0xf) >> 4;
    const int iy3 = (y3 + 0xf) >> 4;

    Vector3D A = dc31 * ((iy2 - iy1)) - dc21 * ((iy3 - iy1));
    Vector3D B = dc21 * ((ix3 - ix1)) - dc31 * ((ix2 - ix1));
    int C = ((iy3 - iy1)) * ((ix2 - ix1)) - ((ix3 - ix1)) * ((iy2 - iy1));

    if(C == 0) return;

    Vector3D dzdx = A / (float)-C;
    Vector3D dzdy = B / (float)-C;




    if(maxY < 0)
        return;
    if(minY >= backBuffer->height)
        return;
    if(maxX < 0)
        return;
    if(minX >= backBuffer->width)
        return;

    if(minY < 0)
        minY = 0;
    if(maxY >= backBuffer->height)
        maxY = backBuffer->height - 1;

    if(minX < 0)
        minX = 0;
    if(maxX >= backBuffer->width)
        maxX = backBuffer->width - 1;


    int blockSize = 8;

    minX &= ~(blockSize - 1);
    minY &= ~(blockSize - 1);



    int* bbPtr = reinterpret_cast<int*>(backBuffer->getBuffer()->getDataPtr());
    bbPtr += minY * backBuffer->width;

    int C1 = Dy12*x1 - Dx12*y1;
    int C2 = Dy23*x2 - Dx23*y2;
    int C3 = Dy31*x3 - Dx31*y3;


    //top-left fill convention fix
    if(Dy12 > 0 || (Dy12 == 0 && Dx12 > 0)) C1--;
    if(Dy23 > 0 || (Dy23 == 0 && Dx23 > 0)) C2--;
    if(Dy31 > 0 || (Dy31 == 0 && Dx31 > 0)) C3--;

    for(int y = minY; y < maxY; y+=blockSize)
    {
        for(int x = minX; x < maxX; x+=blockSize)
        {
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
            int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

            //Skip entire block if all it's 4 corners outside of triangle edges
            if(a == 0 || b == 0 || c == 0)
                continue;

            int* colorBuffer = bbPtr;

            //Whole block is inside triangle!
            if(a == 0xf && b == 0xf && c == 0xf)
            {
                for(int by = 0; by < blockSize; by++)
                {
                    for(int bx = x; bx < (x + blockSize); bx++)
                    {
                        //compute interpolation weights
                        Vector3D interpolators = col1 + dzdx * (bx - ix1) + dzdy*(y + by - iy1);
                        Vector3D invW(1.0f / p0->reg[0].W * interpolators.X, 1.0f / p1->reg[0].W * interpolators.Y, 1.0f / p2->reg[0].W * interpolators.Z);

                        //compute 1/w value for perspective correction
                        pc = 1.0f / (invW.X + invW.Y + invW.Z);

                        //interpolate registers
                        for(int r = 1; r < REG_COUNT; r++)
                        {
                            r_ps.reg[r] = p0->reg[r] * invW.X + p1->reg[r] * invW.Y+ p2->reg[r] * invW.Z;
                            r_ps.reg[r] *= pc; //add perspective correction
                        }

                        //interpolate camera Z
                        float cam_interp =
                            camZ.X * invW.X +
                            camZ.Y * invW.Y +
                            camZ.Z * invW.Z;

                        cam_interp *= pc;


                        //bbPtr[0] = 0xffff00ff;
                        Vector4D rst;
#ifdef RENDER_FFP
                        rst = FFP_GetPixelColor(&r_ps.reg[0], cam_interp);
#else
                        rst = ps->Execute(&r_ps.reg[0]);
#endif


                        if(blendState.BlendEnable)
                        {
                            uint32 dc = colorBuffer[bx];

                            Vector4D dstColor = ConvertColor(dc);

                            float srcAlpha;
                            float dstAlpha;

                            //get source alpha
                            srcAlpha = GetBlendAlpha(blendState.SrcAlpha, rst, dstColor);
                            dstAlpha = GetBlendAlpha(blendState.DstAlpha, rst, dstColor);

                            rst = rst * srcAlpha + dstColor * dstAlpha;
                        }

                        if(!(alpha_test_enable &&  (rst.W < alpha_ref)))
                         colorBuffer[bx] = ConvertColor(rst);


                    }
                    colorBuffer+= backBuffer->width;
                }
            }
            else //Partially covered block
            {
                int CY1 = C1 + Dx12 * y0 - Dy12 * x0;
                int CY2 = C2 + Dx23 * y0 - Dy23 * x0;
                int CY3 = C3 + Dx31 * y0 - Dy31 * x0;

                for(int by = y; by < (y + blockSize); by++)
                {
                    int CX1 = CY1;
                    int CX2 = CY2;
                    int CX3 = CY3;

                    for(int bx = x; bx < (x + blockSize); bx++)
                    {
                        if(CX1 < 0 && CX2 < 0 && CX3 < 0)
                        {
                            Vector3D interpolators = col1 + dzdx*(bx - ix1) + dzdy*(by - iy1);

                            Vector3D invW(1.0f / p0->reg[0].W * interpolators.X, 1.0f / p1->reg[0].W * interpolators.Y, 1.0f / p2->reg[0].W * interpolators.Z);

                            //compute 1/w value for perspective correction
                            pc = 1.0f / (invW.X + invW.Y + invW.Z);

                            //interpolate registers
                            for(int r = 1; r < REG_COUNT; r++)
                            {
                                r_ps.reg[r] = p0->reg[r] * invW.X + p1->reg[r] * invW.Y+ p2->reg[r] * invW.Z;
                                r_ps.reg[r] *= pc; //add perspective correction
                            }

                            //interpolate camera Z
                            float cam_interp =
                                camZ.X * invW.X +
                                camZ.Y * invW.Y +
                                camZ.Z * invW.Z;

                            cam_interp *= pc;


                            Vector4D rst;
#ifdef RENDER_FFP
                            rst = FFP_GetPixelColor(&r_ps.reg[0], cam_interp);
#else
                            rst = ps->Execute(&r_ps.reg[0]);
#endif
                        //	UINT texcolor = ConvertColor(rst);

                            if(blendState.BlendEnable)
                            {
                                uint32 dc = colorBuffer[bx];

                                Vector4D dstColor = ConvertColor(dc);

                                float srcAlpha;
                                float dstAlpha;

                                //get source alpha
                                srcAlpha = GetBlendAlpha(blendState.SrcAlpha, rst, dstColor);
                                dstAlpha = GetBlendAlpha(blendState.DstAlpha, rst, dstColor);

                                rst = rst * srcAlpha + dstColor * dstAlpha;
                            }

                            if(!(alpha_test_enable &&  (rst.W < alpha_ref)))
                             colorBuffer[bx] = ConvertColor(rst);
                        }

                        CX1 -= FDy12;
                        CX2 -= FDy23;
                        CX3 -= FDy31;
                    }
                    CY1 += FDx12;
                    CY2 += FDx23;
                    CY3 += FDx31;

                    colorBuffer += backBuffer->width;
                }
            }
        }
        bbPtr += backBuffer->width * blockSize;
    }
}



void BlockRasterizer::SetAlphaTestEnable(bool enable)
{
    alpha_test_enable = true;
}

void BlockRasterizer::SetAlphaTestRef(float ref)
{
    alpha_ref = ref;
}

void BlockRasterizer::SetAmbientEnable(bool enable)
{
    ambient_enable = enable;
}

void BlockRasterizer::SetAmbientColor(const Vector3D& color)
{
    ambient_color = color;
}