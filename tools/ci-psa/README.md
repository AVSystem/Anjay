This Dockerfile is developed to build PSA examples and run PSA tests.
It contains the basic libs needed to build Anjay and additionaly custom build
of mbed TLS with PSA enabled.

To build the image, you can run the following command from the root of the Anjay
repository:
```bash
docker build --no-cache -f tools/ci-psa/Dockerfile .
```

