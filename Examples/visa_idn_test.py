import pyvisa

rm = pyvisa.ResourceManager()

print("VISA backend:", rm.visalib)

resources = rm.list_resources()
print("Resources found:", resources)

for resource in resources:
    print("\nTrying:", resource)

    try:
        inst = rm.open_resource(resource)
        inst.timeout = 3000

        response = inst.query("*IDN?")

        print("SUCCESS")
        print("IDN:", response.strip())

        inst.close()

    except Exception as e:
        print("FAILED:", e)
