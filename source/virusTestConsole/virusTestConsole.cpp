#include <iostream>

#include "../virusConsoleLib/consoleApp.h"

#include "dsp56kEmu/jitunittests.h"
#include "dsp56kEmu/interpreterunittests.h"

#include "../synthLib/os.h"

using namespace dsp56k;
using namespace virusLib;
using namespace synthLib;

int main(int _argc, char* _argv[])
{
	if constexpr(true)
	{
		try
		{
			puts("Running Unit Tests...");
//			InterpreterUnitTests tests;
			JitUnittests jitTests;
			puts("Unit Tests finished.");
		}
		catch(const std::string& _err)
		{
			std::cout << "Unit test failed: " << _err << std::endl;
			ConsoleApp::waitReturn();
			return -1;
		}
	}

	const auto romFile = findROM(0);

	if(romFile.empty())
	{
		std::cout << "Unable to find ROM. Place a ROM file with .bin extension next to this program." << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	std::unique_ptr<ConsoleApp> app;
	app.reset(new ConsoleApp(romFile));

	if(!app->isValid())
	{
		std::cout << "ROM file " << romFile << " couldn't be loaded. Make sure tha the ROM file is valid" << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	if(_argc > 1)
	{
		const std::string name = _argv[1];
		if(hasExtension(name, ".mid") || hasExtension(name, ".bin"))
		{
			if(!app->loadDemo(name))
			{
				std::cout << "Failed to load demo from '" << name << "', make sure that the file contains a demo" << std::endl;
				ConsoleApp::waitReturn();
				return -1;
			}
		}
		else if(name == "demo")
		{
			if(!app->loadInternalDemo())
			{
				std::cout << "Failed to load internal demo, the ROM might not contain any demo song" << std::endl;
				ConsoleApp::waitReturn();
				return -1;
			}
		}
		else if(!app->loadSingle(name))
		{
			std::cout << "Failed to find preset '" << _argv[1] << "', make sure to use a ROM that contains it" << std::endl;
			ConsoleApp::waitReturn();
			return -1;
		}
	}
	else
	{
		if(!app->loadInternalDemo())
			app->loadSingle(0, 0);
	}

	const std::string audioFilename = app->getSingleNameAsFilename();

	app->run(audioFilename);

	std::cout << "Program ended. Press key to exit." << std::endl;
	ConsoleApp::waitReturn();

	return 0;
}
