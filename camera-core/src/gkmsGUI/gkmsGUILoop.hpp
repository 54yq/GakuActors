

namespace GkmsGUILoop {
	// hostedPanel: the caller already owns an ImGui window (the loader's plugin
	// content panel). Draw the controls straight into it instead of opening a
	// floating "Gakumas Plugin Config" window.
	void mainLoop(bool hostedPanel = false);
}