#include "VirusEditor.h"

#include "BinaryData.h"

#include "../ParameterNames.h"
#include "../PluginProcessor.h"
#include "../VirusController.h"
#include "../VirusParameterBinding.h"
#include "../version.h"

#include "../../synthLib/os.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(VirusParameterBinding& _binding, AudioPluginAudioProcessor &_processorRef, const std::string& _jsonFilename, std::string _skinFolder, std::function<void()> _openMenuCallback) :
		Editor(static_cast<EditorInterface&>(*this)),
		m_processor(_processorRef),
		m_parameterBinding(_binding),
		m_skinFolder(std::move(_skinFolder)),
		m_openMenuCallback(std::move(_openMenuCallback))
	{
		create(_jsonFilename);

		m_parts.reset(new Parts(*this));

		// be backwards compatible with old skins
		if(getTabGroupCount() == 0)
			m_tabs.reset(new Tabs(*this));

		m_midiPorts.reset(new MidiPorts(*this));

		// be backwards compatible with old skins
		if(!getConditionCountRecursive())
			m_fxPage.reset(new FxPage(*this));

		m_patchBrowser.reset(new PatchBrowser(*this));

		m_presetName = findComponentT<juce::Label>("PatchName");
		m_focusedParameterName = findComponentT<juce::Label>("FocusedParameterName");
		m_focusedParameterValue = findComponentT<juce::Label>("FocusedParameterValue");
		m_focusedParameterTooltip = findComponentT<juce::Label>("FocusedParameterTooltip", false);

		if(m_focusedParameterTooltip)
			m_focusedParameterTooltip->setVisible(false);

		m_romSelector = findComponentT<juce::ComboBox>("RomSelector");

		m_playModeSingle = findComponentT<juce::Button>("PlayModeSingle", false);
		m_playModeMulti = findComponentT<juce::Button>("PlayModeMulti", false);

		if(m_playModeSingle && m_playModeMulti)
		{
			m_playModeSingle->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeSingle); };
			m_playModeMulti->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeMulti); };
		}
		else
		{
			m_playModeToggle = findComponentT<juce::Button>("PlayModeToggle");
			m_playModeToggle->onClick = [this]{ setPlayMode(m_playModeToggle->getToggleState() ? virusLib::PlayMode::PlayModeMulti : virusLib::PlayMode::PlayModeSingle); };
		}

		if(m_romSelector)
		{
			if(!_processorRef.isPluginValid())
				m_romSelector->addItem("<No ROM found>", 1);
			else
				m_romSelector->addItem(_processorRef.getRomName(), 1);

			m_romSelector->setSelectedId(1, juce::dontSendNotification);
		}

		getController().onProgramChange = [this] { onProgramChange(); };

		addMouseListener(this, true);

		m_focusedParameterName->setVisible(false);
		m_focusedParameterValue->setVisible(false);

		if(auto* versionInfo = findComponentT<juce::Label>("VersionInfo", false))
		{
		    const std::string message = "DSP 56300 Emulator Version " + std::string(g_pluginVersionString) + " - " __DATE__ " " __TIME__;
			versionInfo->setText(message, juce::dontSendNotification);
		}

		if(auto* versionNumber = findComponentT<juce::Label>("VersionNumber", false))
		{
			versionNumber->setText(g_pluginVersionString, juce::dontSendNotification);
		}

		m_deviceModel = findComponentT<juce::Label>("DeviceModel", false);

		updateDeviceModel();

		auto* presetSave = findComponentT<juce::Button>("PresetSave", false);
		if(presetSave)
			presetSave->onClick = [this] { savePreset(); };

		auto* presetLoad = findComponentT<juce::Button>("PresetLoad", false);
		if(presetLoad)
			presetLoad->onClick = [this] { loadPreset(); };

		m_presetName->setEditable(false, true, true);
		m_presetName->onTextChange = [this]()
		{
			const auto text = m_presetName->getText();
			if (text.trim().length() > 0)
			{
				getController().setSinglePresetName(getController().getCurrentPart(), text);
				onProgramChange();
			}
		};

		auto* menuButton = findComponentT<juce::Button>("Menu", false);

		if(menuButton)
			menuButton->onClick = m_openMenuCallback;

		updatePresetName();
		updatePlayModeButtons();
		updateControlLabelAndTooltip(nullptr);
	}

	VirusEditor::~VirusEditor()
	{
		m_parameterBinding.clearBindings();

		getController().onProgramChange = nullptr;
	}

	Virus::Controller& VirusEditor::getController() const
	{
		return m_processor.getController();
	}

	const char* VirusEditor::findNamedResourceByFilename(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	const char* VirusEditor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		if(!m_skinFolder.empty())
		{
			auto readFromCache = [this, &_name, &_dataSize]()
			{
				const auto it = m_fileCache.find(_name);
				if(it == m_fileCache.end())
				{
					_dataSize = 0;
					return static_cast<char*>(nullptr);
				}
				_dataSize = static_cast<uint32_t>(it->second.size());
				return &it->second.front();
			};

			auto* res = readFromCache();

			if(res)
				return res;

			const auto modulePath = synthLib::getModulePath();
			const auto folder = synthLib::validatePath(m_skinFolder.find(modulePath) == 0 ? m_skinFolder : modulePath + m_skinFolder);

			// try to load from disk first
			FILE* hFile = fopen((folder + _name).c_str(), "rb");
			if(hFile)
			{
				fseek(hFile, 0, SEEK_END);
				_dataSize = ftell(hFile);
				fseek(hFile, 0, SEEK_SET);

				std::vector<char> data;
				data.resize(_dataSize);
				const auto readCount = fread(&data.front(), 1, _dataSize, hFile);
				fclose(hFile);

				if(readCount == _dataSize)
					m_fileCache.insert(std::make_pair(_name, std::move(data)));

				res = readFromCache();

				if(res)
					return res;
			}
		}

		uint32_t size = 0;
		const auto res = findNamedResourceByFilename(_name, size);
		if(!res)
			throw std::runtime_error("Failed to find file named " + _name);
		_dataSize = size;
		return res;
	}

	int VirusEditor::getParameterIndexByName(const std::string& _name)
	{
		return getController().getParameterIndexByName(_name);
	}

	bool VirusEditor::bindParameter(juce::Button& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}

	bool VirusEditor::bindParameter(juce::ComboBox& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}

	bool VirusEditor::bindParameter(juce::Slider& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}

	juce::Value* VirusEditor::getParameterValue(int _parameterIndex)
	{
		return getController().getParamValueObject(_parameterIndex);
	}

	void VirusEditor::onProgramChange()
	{
		m_parts->onProgramChange();
		updatePresetName();
		updatePlayModeButtons();
		updateDeviceModel();
	}

	void VirusEditor::onPlayModeChanged()
	{
		m_parts->onPlayModeChanged();
		updatePresetName();
		updatePlayModeButtons();
	}

	void VirusEditor::onCurrentPartChanged()
	{
		m_parts->onCurrentPartChanged();
		updatePresetName();
	}

	void VirusEditor::mouseDrag(const juce::MouseEvent & event)
	{
		// During mouse drags, updates to the control label are handled via
		//  Controller::sendParameterChange -> VirusEditor::updateControlLabel(const pluginLib::Parameter),
		// which is not well placed to handle FocusedParameterTooltip updates, as it lacks a reference to the
		// GUI component being manipulated. So, we do that work here.

		if(m_focusedParameterTooltip){
			auto _component = event.eventComponent;
			const auto& props = _component->getProperties();
			const int v = props["parameter"];
			const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(getController().getCurrentPart());
			const auto* p = getController().getParameter(v, part);

			if(!p) // Only occurs when we have a mouse drag across a non parameter UI feature (i.e. the skin background)...
				m_focusedParameterTooltip->setVisible(false);

			const auto value = p->getText(p->getValue(), 0);
			updateFocusedParameterTooltip(_component, value);
		}
	}

	void VirusEditor::mouseEnter(const juce::MouseEvent& event)
	{
		if (event.mouseWasDraggedSinceMouseDown())
			return;
		updateControlLabelAndTooltip(event.eventComponent);
	}
	void VirusEditor::mouseExit(const juce::MouseEvent& event)
	{
		if (event.mouseWasDraggedSinceMouseDown())
			return;
		updateControlLabelAndTooltip(nullptr);
	}

	void VirusEditor::mouseUp(const juce::MouseEvent& event)
	{
		if (event.mouseWasDraggedSinceMouseDown())
			return;
		updateControlLabelAndTooltip(event.eventComponent);
	}

	void VirusEditor::updateControlLabelAndTooltip(juce::Component* _component) const
	{
		if(_component && !_component->getProperties().contains("parameter"))
			_component = _component->getParentComponent(); // combo boxes report the child label as event source, try the parent in this case


		// If there's no component, clear the control label and tooltip and return.
		if(!_component || !_component->getProperties().contains("parameter"))
		{
			m_focusedParameterName->setVisible(false);
			m_focusedParameterValue->setVisible(false);
			if(m_focusedParameterTooltip)
				m_focusedParameterTooltip->setVisible(false);

			return;
		}

		const auto& props = _component->getProperties();
		const int v = props["parameter"];
		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(getController().getCurrentPart());
		const auto* p = getController().getParameter(v, part);

		// If the parameter lookup returns a nullptr, then clear the label and tooltip and return.
		if(!p)
		{
			m_focusedParameterName->setVisible(false);
			m_focusedParameterValue->setVisible(false);
			if(m_focusedParameterTooltip)
				m_focusedParameterTooltip->setVisible(false);

			return;
		}

		// else, set the control label and tooltip
		const auto value = p->getText(p->getValue(), 0);
		const auto& desc = p->getDescription();

		m_focusedParameterName->setText(desc.displayName, juce::dontSendNotification);
		m_focusedParameterValue->setText(value, juce::dontSendNotification);
		m_focusedParameterName->setVisible(true);
		m_focusedParameterValue->setVisible(true);

		if(m_focusedParameterTooltip)
		{
			// For skins that implement FocusedParameterTooltip (e.g. Trancy)
			updateFocusedParameterTooltip(_component, value);
		}
	}

	void VirusEditor::updateFocusedParameterTooltip(const juce::Component *_component, const juce::String &value) const {

		if(!dynamic_cast<const juce::Slider*>(_component))
			return;

		if(!_component || !_component->getProperties().contains("parameter"))
		{
			m_focusedParameterTooltip->setVisible(false);
			return;
		}


		const auto& props = _component->getProperties();
		const int v = props["parameter"];
		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(getController().getCurrentPart());
		const auto* p = getController().getParameter(v, part);


		// We only render the tooltip if the parameter being changed is on the same 'part' as the currently visible/selected one.
		if(p->getPart() != getController().getCurrentPart()) {
			m_focusedParameterTooltip->setVisible(false);
			return;
		}

		// and even furthermore, it's only rendered if the parameter is on the currently visible page of the editor.
		jassert(getTabGroupCount() == 1);
		auto tabGroup = getTabGroupsByName().begin()->second;
		bool isOnPage = tabGroup->searchPage(_component);
		if(!isOnPage) {
			m_focusedParameterTooltip->setVisible(false);
			return;
		}

		//
		// Finally, we are ready to draw the tooltip.
		//

		int x = _component->getX();
		int y = _component->getY();

		// local to global
		auto parent = _component->getParentComponent();

		while(parent && parent != this)
		{
			x += parent->getX();
			y += parent->getY();
			parent = parent->getParentComponent();
		}

		x += (_component->getWidth()>>1) - (m_focusedParameterTooltip->getWidth() >> 1);
		y += _component->getHeight() + (m_focusedParameterTooltip->getHeight() >> 1);

		// global to local of tooltip parent
		parent = m_focusedParameterTooltip->getParentComponent();

		while(parent && parent != this)
		{
			x -= parent->getX();
			y -= parent->getY();
			parent = parent->getParentComponent();
		}

		if(m_focusedParameterTooltip->getProperties().contains("offsetY"))
			y += static_cast<int>(m_focusedParameterTooltip->getProperties()["offsetY"]);

		m_focusedParameterTooltip->setTopLeftPosition(x, y);
		m_focusedParameterTooltip->setText(value, juce::dontSendNotification);
		m_focusedParameterTooltip->setVisible(true);
		m_focusedParameterTooltip->toFront(false);
	}


	void VirusEditor::updateControlLabel(const pluginLib::Parameter &_parameter) const
	{
		/*
		 * To update the control label with the status of a parameter that doesn't have a GUI component
		 * instantiated, updateControlLabelAndTooltip(juce::Component* _component) will not work for us, as it requires a
		 * reference to the GUI component being manipulated.
		 *
		 * So we instead implement another version of it here, which updates the label using information provided by _parameter.
		 *
		 * Updating the tooltip for skins that implement FocusedParameterTooltip is left to VirusEditor::mouseDrag to trigger.
		 * */

		const auto value = _parameter.getText(_parameter.getValue(), 0);

		juce::String name;
		if(_parameter.getPart() == getController().getCurrentPart()){
			name = _parameter.getDescription().name; // i.e. 'Portamento Time'
			// It looks better if we don't show the 'part' number if we already have that part selected.
		} else {
			name = _parameter.name; // i.e. 'Ch 1 Portamento Time'
			// and, this way we give the user a hint that they are manipulating an off-screen parameter.
		}

		m_focusedParameterName->setText(name, juce::dontSendNotification);
		m_focusedParameterValue->setText(value, juce::dontSendNotification);

		m_focusedParameterName->setVisible(true);
		m_focusedParameterValue->setVisible(true);
	}

	void VirusEditor::updatePresetName() const
	{
		m_presetName->setText(getController().getCurrentPartPresetName(getController().getCurrentPart()), juce::dontSendNotification);
	}

	void VirusEditor::updatePlayModeButtons() const
	{
		if(m_playModeSingle)
			m_playModeSingle->setToggleState(!getController().isMultiMode(), juce::dontSendNotification);
		if(m_playModeMulti)
			m_playModeMulti->setToggleState(getController().isMultiMode(), juce::dontSendNotification);
		if(m_playModeToggle)
			m_playModeToggle->setToggleState(getController().isMultiMode(), juce::dontSendNotification);
	}

	void VirusEditor::updateDeviceModel()
	{
		if(!m_deviceModel)
			return;

		std::string m;

		switch(getController().getVirusModel())
		{
		case virusLib::A: m = "A";		break;
		case virusLib::B: m = "B";		break;
		case virusLib::C: m = "C";		break;
		case virusLib::TI: m = "TI";	break;
		}

		m_deviceModel->setText(m, juce::dontSendNotification);
		m_deviceModel = nullptr;	// only update once
	}

	void VirusEditor::savePreset()
	{
		const auto path = getController().getConfig()->getValue("virus_bank_dir", "");
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Save preset as syx",
			m_previousPath.isEmpty()
			? (path.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : juce::File(path))
			: m_previousPath,
			"*.syx", true);

		constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		auto onFileChooser = [this](const juce::FileChooser& chooser)
		{
			if (chooser.getResults().isEmpty())
				return;

			const auto result = chooser.getResult();
			m_previousPath = result.getParentDirectory().getFullPathName();
			const auto ext = result.getFileExtension().toLowerCase();

			const auto data = getController().createSingleDump(getController().getCurrentPart(), virusLib::toMidiByte(virusLib::BankNumber::A), 0);

			if(!data.empty())
			{
				result.deleteFile();
				result.create();
				result.appendData(&data[0], data.size());
			}
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
	}

	void VirusEditor::loadPreset()
	{
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Choose syx/midi banks to import",
			m_previousPath.isEmpty()
			? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory()
			: m_previousPath,
			"*.syx,*.mid,*.midi", true);

		constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		const std::function onFileChooser = [this](const juce::FileChooser& chooser)
		{
			if (chooser.getResults().isEmpty())
				return;

			const auto result = chooser.getResult();
			m_previousPath = result.getParentDirectory().getFullPathName();
			const auto ext = result.getFileExtension().toLowerCase();

			std::vector<Patch> patches;
			PatchBrowser::loadBankFile(getController(), patches, nullptr, result);

			if (patches.empty())
				return;

			if (patches.size() == 1)
			{
				// load to edit buffer of current part
				const auto data = getController().modifySingleDump(patches.front().sysex, virusLib::BankNumber::EditBuffer, 
					getController().isMultiMode() ? getController().getCurrentPart() : virusLib::SINGLE, true, true);
				getController().sendSysEx(data);
			}
			else
			{
				// load to bank A
				for (const auto& p : patches)
				{
					const auto data = getController().modifySingleDump(p.sysex, virusLib::BankNumber::A, 0, true, false);
					getController().sendSysEx(data);
				}
			}

			getController().onStateLoaded();
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
	}

	void VirusEditor::setPlayMode(uint8_t _playMode)
	{
		const auto playMode = getController().getParameterIndexByName(Virus::g_paramPlayMode);

		getController().getParameter(playMode)->setValue(_playMode);

		if (_playMode == virusLib::PlayModeSingle && getController().getCurrentPart() != 0)
			m_parameterBinding.setPart(0);

		onPlayModeChanged();
	}

	void VirusEditor::setPart(size_t _part)
	{
		m_parameterBinding.setPart(static_cast<uint8_t>(_part));
		onCurrentPartChanged();
	}
}
