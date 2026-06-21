#include "stdafx.h"
#include "ExpOverPool.h"
//#include "ExpOverPool_TaggedPointer.h"	//TaggedPointer로 사용하려면 이 주석을 해제하고, ExpOverPool.cpp의 g_ExpOverPool 선언도 바꾼다.

// 전역 객체 정의 (ODR — 이 .cpp 하나에만 있어야 함)
ExpOverPool g_ExpOverPool;
//ExpOverPoolTagged g_ExpOverPool;
