import wmi

# Example program from python's website.
c = wmi.WMI()
for s in c.Win32_Service(StartMode="Auto", State="Stopped"):
    if input("Restart %s? " % s.Caption).upper() == "Y":
        s.StartService()