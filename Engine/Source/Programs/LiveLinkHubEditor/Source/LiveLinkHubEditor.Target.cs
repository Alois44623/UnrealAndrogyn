// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildBase;
using UnrealBuildTool;


[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class LiveLinkHubEditorTarget : TargetRules
{
	// Restrict OptedInModulePlatforms to the current Target.Platform.
	// Used during staging, which otherwise fails in TargetPlatform-related restricted
	// subdirectories (Engine/Binaries/Win64/{Android,IOS,Linux,LinuxArm64,...}).
	[CommandLine("-SingleModulePlatform")]
	public bool bSingleModulePlatform = false;

	// Whether to enable building Capture Manager plugin.
	[CommandLine("-EnableCaptureManagerPlugin=")]
	public bool bEnableCaptureManagerPlugin = false;

	public LiveLinkHubEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		bExplicitTargetForType = true;
		bGenerateProgramProject = true;

		SolutionDirectory = "Programs/LiveLink";
		LaunchModuleName = "LiveLinkHubLauncher";
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// These plugins are required for running LiveLinkHub. 
		// They may be a direct dependency or a dependency of another plugin.
		EnablePlugins.AddRange(new string[]
		{
			"LiveLink",
			"LiveLinkHub",
			"LiveLinkCamera",
			"LiveLinkLens",
			"LensComponent",
			"LiveLinkInputDevice",
			"ContentBrowserAssetDataSource",
			"ProceduralMeshComponent",
			"PropertyAccessEditor",
			"PythonScriptPlugin",
			"StructUtils",
			"UdpMessaging",
			"CameraCalibrationCore",
			"AppleARKitFaceSupport",
			"XInputDevice"
		});

		if (bEnableCaptureManagerPlugin)
		{
			OptionalPlugins.AddRange(new string[]
			{
				"CaptureManagerApp",
				"CaptureManagerCore"
			});
		}

		if (bSingleModulePlatform)
		{
			// Necessary for staging, but avoided otherwise because it dirties
			// Definitions.CookedEditor.h and triggers rebuilds (incl. UnrealEditor).
			OptedInModulePlatforms = new UnrealTargetPlatform[] { Target.Platform };
		}

		bAllowEnginePluginsEnabledByDefault = false;
		bBuildAdditionalConsoleApp = false;

		// Based loosely on the VCProject.cs logic to construct NMakePath.
		string BaseExeName = "LiveLinkHubEditor";
		OutputFile = "Binaries/" + Platform.ToString() + "/" + BaseExeName;
		if (Configuration != UndecoratedConfiguration)
		{
			OutputFile += "-" + Platform.ToString() + "-" + Configuration.ToString();
		}
		if (Platform == UnrealTargetPlatform.Win64)
		{
			OutputFile += ".exe";
		}

		// Copy the target receipt into the project binaries directory.
		// Prevents "Would you like to build the editor?" prompt on startup
		// when running with project context.
		DirectoryReference ReceiptSrcDir = Unreal.EngineDirectory;
		DirectoryReference ReceiptDestDir = DirectoryReference.Combine(
			Unreal.EngineDirectory, "Source", "Programs", "LiveLinkHubEditor");

		FileReference ReceiptSrcPath = TargetReceipt.GetDefaultPath(ReceiptSrcDir, BaseExeName, Platform, Configuration, Architectures);
		FileReference ReceiptDestPath = TargetReceipt.GetDefaultPath(ReceiptDestDir, BaseExeName, Platform, Configuration, Architectures);

		PostBuildSteps.Add($"echo Copying \"{ReceiptSrcPath}\" to \"{ReceiptDestPath}\"");
		DirectoryReference.CreateDirectory(ReceiptDestPath.Directory);

		if (Platform == UnrealTargetPlatform.Win64)
		{
			PostBuildSteps.Add($"copy /Y \"{ReceiptSrcPath}\" \"{ReceiptDestPath}\"");
		}
		else
		{
			PostBuildSteps.Add($"cp -a \"{ReceiptSrcPath}\" \"{ReceiptDestPath}\"");
		}
	}
}
