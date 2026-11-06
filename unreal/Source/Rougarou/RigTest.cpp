#include "Rig.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigTest, "Rougarou.Rig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRigTest::RunTest(const FString& Parameters)
{
	FVector Bend, End;
	const FVector Start = FVector::ZeroVector;
	FRig::Solve(Start, FVector(1, 0, 0), FVector(0, 0, 1), 1, 1, Bend, End);
	TestTrue(TEXT("reachable contact is preserved"), End.Equals(FVector(1, 0, 0), 0.001f));
	TestTrue(TEXT("upper segment keeps its length"), FMath::IsNearlyEqual(Bend.Size(), 1.f, 0.001f));
	TestTrue(TEXT("lower segment keeps its length"), FMath::IsNearlyEqual((End - Bend).Size(), 1.f, 0.001f));
	TestTrue(TEXT("elbow follows its pole"), Bend.Z > 0);
	FRig::Solve(Start, FVector(4, 0, 0), FVector(1, 0, 0), 1, 1, Bend, End);
	TestTrue(TEXT("stretch remains bounded"), End.Size() <= 2.3f);
	TestFalse(TEXT("collinear pole remains finite"), Bend.ContainsNaN() || End.ContainsNaN());
	FRig::Solve(Start, Start, Start, 1, 1, Bend, End);
	TestFalse(TEXT("coincident target remains finite"), Bend.ContainsNaN() || End.ContainsNaN());
	const FVector Axis(0, 0, -1), Front(-1, 0, 0);
	const FQuat Rest(FVector::UpVector, 0.7f);
	const FVector Targets[] = { FVector(0, 10, -70), FVector(0, 25, 15), FVector(0, -30, -10), FVector(0, 0, -120) };
	for (float Side : { -1.f, 1.f })
	{
		const FVector Hip(22, Side * 10, 100);
		const FVector Foot(17, Side * 18, 40);
		const FVector Hint = FRig::Knee(FVector::RightVector, FVector::UpVector, FVector::ForwardVector, Side, Foot.Z - Hip.Z);
		FRig::Solve(Hip, Foot, Hip + Hint, 46, 44, Bend, End);
		TestTrue(TEXT("knees stay outside the wall"), Bend.X > 5);
		TestTrue(TEXT("knees open to their own side"), (Bend.Y - Hip.Y) * Side > 0);
		TestTrue(TEXT("knees do not fold behind the hips"), Bend.X < Hip.X);
		for (const FVector& Target : Targets)
		{
			FRig::Solve(Start, Target, FVector(35, Side * 30, 100), 46, 44, Bend, End);
			const FVector Directions[] = { Bend - Start, End - Bend };
			const FVector Facings[] = { Bend - End, Bend - Start };
			for (int32 Segment = 0; Segment < 2; Segment++)
			{
				const FVector Direction = Directions[Segment].GetSafeNormal();
				const FVector Facing = FVector::VectorPlaneProject(Facings[Segment], Direction).GetSafeNormal();
				const FQuat Turn = FRig::Orient(Rest, Axis, Front, Direction, Facing) * Rest.Inverse();
				TestTrue(TEXT("leg axis follows its segment"), Turn.RotateVector(Axis).Equals(Direction, 0.001f));
				TestTrue(TEXT("kneecap faces the bend plane"), Turn.RotateVector(Front).Equals(Facing, 0.001f));
				TestTrue(TEXT("leg rotation remains normalized"), Turn.IsNormalized());
			}
		}
	}
	const FVector Hip(22, 0, 100), Foot(17, -25, 120);
	const FVector Hint = FRig::Knee(FVector::RightVector, FVector::UpVector, FVector::ForwardVector, -1, Foot.Z - Hip.Z);
	FRig::Solve(Hip, Foot, Hip + Hint, 46, 44, Bend, End);
	const float Upper = (Bend - Hip).Size(), Lower = (End - Bend).Size();
	FRig::Limit(Hip, End, FPlane(FVector::ZeroVector, FVector::ForwardVector), 10, Bend);
	TestTrue(TEXT("high-step knee clears the wall"), Bend.X >= 9.999f);
	TestTrue(TEXT("high-step knee leads above the foot"), Bend.Z > End.Z);
	TestTrue(TEXT("clearance preserves the thigh length"), FMath::IsNearlyEqual((float)(Bend - Hip).Size(), Upper, 0.001f));
	TestTrue(TEXT("clearance preserves the calf length"), FMath::IsNearlyEqual((float)(End - Bend).Size(), Lower, 0.001f));
	return true;
}

#endif
