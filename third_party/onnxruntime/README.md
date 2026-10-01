ONNX Runtime's C API headers (v1.30.0), from https://github.com/microsoft/onnxruntime, MIT license (see LICENSE).

lahn doesn't link ONNX Runtime: these headers only describe its interface. The library itself comes with the optional
stems add-on, and is loaded at run time when that's installed (src/app/stemmodel.cpp).
