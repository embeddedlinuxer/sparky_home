
TOP                 = G:\tools\sparky
INSTALL_TOP         = G:\Qt\tools\QtInstallerFramework\4.0\bin
BINARYCREATOR       = $(INSTALL_TOP)\binarycreator.exe
SETUP               = $(TOP)\install\setup.exe
INSTALL_DATA        = $(TOP)\install\packages\com.pdi.product\data
RELEASE             = $(TOP)\build-sparky-Desktop_Qt_5_15_2_MinGW_32_bit-Release\release
APP                 = $(RELEASE)\Sparky.exe
CONFIG              = $(TOP)\install\config\config.xml
PACKAGES            = $(TOP)\install\packages
INSTALLERBASE       = $(INSTALL_TOP)\installerbase.exe
OPTIONS             = --offline-only


all: copy_executable
	$(BINARYCREATOR) $(OPTIONS) -c $(CONFIG) -p $(PACKAGES) -t $(INSTALLERBASE) $(SETUP)

copy_executable:
	cp $(APP) $(INSTALL_DATA)

clean:
	rm -rf $(SETUP)
