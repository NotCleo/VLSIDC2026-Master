import onnx

def list_onnx_parameters(onnx_model_path):
    """
    Loads an ONNX model and lists its parameters (initializers).

    Args:
        onnx_model_path (str): The path to the ONNX model file.
    """
    try:
        # Load the ONNX model
        model = onnx.load(onnx_model_path)
        graph = model.graph

        print(f"Parameters (Initializers) in ONNX model '{onnx_model_path}':")
        if graph.initializer:
            for initializer in graph.initializer:
                print(f"  Name: {initializer.name}")
                print(f"  Shape: {list(initializer.dims)}")
                print(f"  Data Type: {onnx.helper.tensor_dtype_to_string(initializer.data_type)}")
                # You can access the actual parameter data if needed (e.g., initializer.float_data)
                print("-" * 20)
        else:
            print("No initializers (parameters) found in this ONNX model.")

    except Exception as e:
        print(f"Error loading or parsing ONNX model: {e}")
