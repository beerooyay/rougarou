#include "ClimbSim.h"
#include "GymActor.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// run headless: UnrealEditor-Cmd Rougarou.uproject -ExecCmds="Automation RunTests Rougarou; Quit" -unattended -nullrhi
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClimbSimTest, "Rougarou.ClimbSim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static void Settle(FClimbSim& Sim, int32 Frames)
{
	for (int32 Frame = 0; Frame < Frames; Frame++) Sim.Step(1.f / 60);
	Sim.Lean = FVector2D::ZeroVector;
}

bool FClimbSimTest::RunTest(const FString& Parameters)
{
	FClimbSim Sim;
	if (!FClimbSpec::Load(FPaths::ProjectContentDir() / TEXT("Data/routes.json"), Sim.Spec))
	{
		AddError(TEXT("routes.json must load"));
		return false;
	}
	Sim.Spec.Routes = AGymActor::Problems();
	for (Sim.Route = 0; Sim.Route < Sim.Spec.Routes.Num(); Sim.Route++)
	{
		const FString& Name = Sim.Spec.Routes[Sim.Route].Name;
		Sim.Restart();
		Settle(Sim, 180);
		if (Sim.Grips[0] < 0 || Sim.Grips[1] < 0 || Sim.Grips[2] < 0 || Sim.Grips[3] < 0)
			AddError(FString::Printf(TEXT("%s: initial grips must survive a static hang"), *Name));
		if (Sim.Grab(2, Sim.Holds().Num() - 1))
			AddError(FString::Printf(TEXT("%s: the top hold must be out of reach from the start"), *Name));

		// greedy climber: try every limb on every hold, optionally after a hip shift, and keep the move that progresses most without losing a hand
		Sim.Restart();
		const bool Across = false;
		const FVector2D Shifts[] = { { -1, 0 }, { 1, 0 }, { 0, 1 }, { 0, -1 } };
		auto Score = [&]()
		{
			float Total = Sim.Won ? 1000 : 0;
			for (int32 Limb = 0; Limb < 4; Limb++)
			{
				// a trailing foot swinging free after a high step is the next move, not a failure
				if (Sim.Grips[Limb] < 0) { Total -= Limb < 2 ? 0.1f : 0.3f; continue; }
				const FVector2D Point = Sim.Holds()[Sim.Grips[Limb]].Point;
				// progress is the hands; feet follow, and a foot jammed above the hips is a dead end, not progress.
				// traverses still reward staying high, or the greedy line sinks below the finish
				Total += (Across ? Point.X + Point.Y * 0.3f : Point.Y) * (Limb >= 2 ? 1 : 0.3f);
				if (Limb < 2 && Point.Y > Sim.Position.Y) Total -= 1;
			}
			return Total;
		};
		for (int32 Move = 0; Move < 24 && !Sim.Won; Move++)
		{
			const FClimbSim::FState Saved = Sim.Save();
			float Best = Score() + 0.01f;
			int32 Choice[3] = { -1, -1, -1 };
			for (int32 Lean = -1; Lean < 4; Lean++)
				for (int32 Limb = 0; Limb < 4; Limb++)
					for (int32 Hold = 0; Hold < Sim.Holds().Num(); Hold++)
					{
						if (Sim.Grips[Limb] == Hold) continue;
						Sim.Load(Saved);
						if (Lean >= 0)
						{
							Sim.Lean = Shifts[Lean] * 0.6f;
							Settle(Sim, 30);
							if (Sim.Grips[2] < 0 || Sim.Grips[3] < 0) continue;
						}
						if (!Sim.Grab(Limb, Hold)) continue;
						Settle(Sim, 45);
						if (Sim.Grips[2] < 0 || Sim.Grips[3] < 0) continue;
						const float Result = Score();
						if (Result > Best)
						{
							Best = Result;
							Choice[0] = Lean;
							Choice[1] = Limb;
							Choice[2] = Hold;
						}
					}
			Sim.Load(Saved);
			if (Choice[1] < 0) break;
			if (Choice[0] >= 0)
			{
				Sim.Lean = Shifts[Choice[0]] * 0.6f;
				Settle(Sim, 30);
			}
			Sim.Grab(Choice[1], Choice[2]);
			Settle(Sim, 45);
			AddInfo(FString::Printf(TEXT("%s: limb %d to %d height=%.2f"), *Name, Choice[1], Choice[2], Sim.Position.Y));
		}
		if (!Sim.Won) AddError(FString::Printf(TEXT("%s must be solvable"), *Name));
	}
	return true;
}

#endif
