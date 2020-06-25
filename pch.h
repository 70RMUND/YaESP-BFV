// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#ifndef PCH_H
#define PCH_H

// TODO: add headers that you want to pre-compile here
#include <stdio.h>
#include <Windows.h>
#include <offsets.h>

#define ValidPointer( pointer ) ( pointer != NULL && (DWORD_PTR)pointer >= 0x10000 && (DWORD_PTR)pointer < 0x000F000000000000 /*&& some other checks*/ )
#define RETURN_IF_BAD( pointer ) if (!( pointer != NULL && (DWORD_PTR)pointer >= 0x10000 && (DWORD_PTR)pointer < 0x000F000000000000)) return
#define CONTINUE_IF_BAD( pointer ) if (!( pointer != NULL && (DWORD_PTR)pointer >= 0x10000 && (DWORD_PTR)pointer < 0x000F000000000000)) continue
#define TRY __try{
#define TRY_END }__except(1){;};


struct Vec2 { union { float v[2]; struct { float x; float y; }; }; };
struct Vec3 { union { float v[3]; struct { float x; float y; float z; }; }; };
struct Vec4 { public: union { float v[4]; struct { float x; float y; float z; float w; }; }; };
static Vec4 asVec4V(float x, float y, float z, float w) { Vec4 out; out.x = x; out.y = y; out.z = z; out.w = w; return out; }
struct Matrix4x4 { union { Vec4 v[4]; float m[4][4]; struct { Vec4 right; Vec4 up; Vec4 forward; Vec4 trans; }; }; };
typedef Matrix4x4 LinearTransform;

class AxisAlignedBox
{
public:
	Vec4 min;
	Vec4 max;
};


struct AxisAlignedBox2
{
	Vec4 min;
	Vec4 max;
	Vec4 crnr2;
	Vec4 crnr3;
	Vec4 crnr4;
	Vec4 crnr5;
	Vec4 crnr6;
	Vec4 crnr7;

	/*
	   .5------8
	 .' |    .'|
	6---+--7'  |
	|   |  |   |
	|  ,4--+---3
	|.'    | .'
	1------2'
	1 = min
	8 = max
	*/

	void updateBox(AxisAlignedBox *box_in)
	{
		this->crnr2 = asVec4V(box_in->max.x, box_in->min.y, box_in->min.z, 0.0);
		this->crnr3 = asVec4V(box_in->max.x, box_in->min.y, box_in->max.z, 0.0);
		this->crnr4 = asVec4V(box_in->min.x, box_in->min.y, box_in->max.z, 0.0);
		this->crnr5 = asVec4V(box_in->min.x, box_in->max.y, box_in->max.z, 0.0);
		this->crnr6 = asVec4V(box_in->min.x, box_in->max.y, box_in->min.z, 0.0);
		this->crnr7 = asVec4V(box_in->max.x, box_in->max.y, box_in->min.z, 0.0);

		this->min = box_in->min;
		this->max = box_in->max;
	}
};



class QuatTransform
{
public:
	Vec4 m_TransAndScale; //0x0000 
	Vec4 m_Rotation; //0x0010 
};//Size=0x0020

#define D3DX_PI    (3.14159265358979323846)

//#define printf(x) x == x;



#endif //PCH_H