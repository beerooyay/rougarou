#include "ClimbView.h"
#include "GarmentPhysics.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationTest,"Rougarou.Presentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPresentationTest::RunTest(const FString& Parameters)
{
 FGarmentPhysics Garments;FVector Tips[8];for(int32 I=0;I<8;I++)Tips[I]=FVector(12,I*10,100);
 const TArray<FGarmentCapsule> Body={{FVector::ZeroVector,FVector(0,0,200),10}};
 Garments.Step(Tips,Body,1.f/60,true);
 for(int32 Frame=0;Frame<180;Frame++)Garments.Step(Tips,Body,1.f/60,true,FVector(-20,0,0));
 TestTrue(TEXT("cloth masses collide with the body"),(Tips[0]+Garments.Offset[0]).X>=10.99);
 for(int32 I=0;I<8;I++)TestTrue(TEXT("stable garment displacement remains bounded under wind"),Garments.Offset[I].Size()<=(I<4?1.001:I<6?.501:.701)&&!Garments.Offset[I].ContainsNaN());
 TestTrue(TEXT("cloth responds to gravity"),Garments.Offset[0].Z<0);
 Garments.Step(Tips,Body,1.f/60,false);TestTrue(TEXT("disabling fabric resets its masses"),Garments.Offset[0].IsNearlyZero());
 Garments.Step(Tips,Body,1.f/60,true,FVector::ZeroVector,3.f);
 for(int32 Frame=0;Frame<180;Frame++)Garments.Step(Tips,Body,1.f/60,true,FVector(-20,0,0),3.f);
 for(int32 I=0;I<8;I++)TestTrue(TEXT("personal garment motion cannot exceed three centimetres"),Garments.Offset[I].Size()<=(I<4?3.001:I<6?1.501:2.101)&&!Garments.Offset[I].ContainsNaN());
 Garments.Step(Tips,Body,1.f/60,false);TestTrue(TEXT("personal fabric resets when disabled"),Garments.Offset[0].IsNearlyZero());
 FClimbRoute Route;
 Route.Holds={{{0,1.5},TEXT("jug"),{0,-1}},{{.4,1.5},TEXT("jug"),{0,-1}}};
 const FVector Eye(100,0,150);
 TestEqual(TEXT("crosshair selects the hold on its ray"),FClimbView::HoldInSight(Route,Eye,FVector(-1,0,0)),0);
 TestEqual(TEXT("holds behind the camera cannot be selected"),FClimbView::HoldInSight(Route,Eye,FVector(1,0,0)),INDEX_NONE);
 TestEqual(TEXT("empty wall does not select a distant off-axis hold"),FClimbView::HoldInSight(Route,Eye,FVector(0,1,0)),INDEX_NONE);
 TestTrue(TEXT("eyes retain wall clearance"),FClimbView::ClearEye(Route,FVector(2,0,150)).X>=18);
 return true;
}
#endif
