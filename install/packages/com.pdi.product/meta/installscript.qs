function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    component.createOperations();

     if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/Sparky.exe", "@DesktopDir@/Sparky.lnk");
    }
}
