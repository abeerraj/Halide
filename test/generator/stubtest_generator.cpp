#include "Halide.h"

namespace {

enum class BagType { Paper, Plastic };

template<typename Type, int size = 32, int dim = 3>
Halide::Buffer<Type> make_image(int extra) {
    Halide::Buffer<Type> im(size, size, dim);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < dim; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

class StubTest : public Halide::Generator<StubTest> {
public:
    GeneratorParam<Type> untyped_buffer_output_type{ "untyped_buffer_output_type", Float(32) };
    GeneratorParam<float> float_param{ "float_param", 3.1415926535f };
    GeneratorParam<BagType> bag_type{ "bag_type",
                                      BagType::Paper,
                                      { { "paper", BagType::Paper },
                                        { "plastic", BagType::Plastic } } };
    GeneratorParam<bool> vectorize{ "vectorize", true };
    GeneratorParam<LoopLevel> intermediate_level{ "intermediate_level", LoopLevel::root() };

    Input<Buffer<uint8_t>> typed_buffer_input{ "typed_buffer_input", 3 };
    Input<Buffer<>> untyped_buffer_input{ "untyped_buffer_input" };
    Input<Buffer<uint8_t>[2]> array_buffer_input{ "array_buffer_input", 3 };
    Input<Func> simple_input{ "simple_input", 3 };  // require a 3-dimensional Func but leave Type unspecified
    Input<Func[]> array_input{ "array_input", 3 };  // require a 3-dimensional Func but leave Type and ArraySize unspecified
    // Note that Input<Func> does not (yet) support Tuples
    Input<float> float_arg{ "float_arg", 1.0f, 0.0f, 100.0f };
    Input<int32_t[]> int_arg{ "int_arg", 1 };  // leave ArraySize unspecified

    Output<Func> simple_output{ "simple_output", Float(32), 3};
    Output<Func> tuple_output{"tuple_output", 3};  // require a 3-dimensional Func but leave Type(s) unspecified
    Output<Func[]> array_output{ "array_output", Int(16), 2};   // leave ArraySize unspecified
    Output<Buffer<float>> typed_buffer_output{ "typed_buffer_output" };
    Output<Buffer<>> untyped_buffer_output{ "untyped_buffer_output" };
    Output<Buffer<>> tupled_output{ "tupled_output", { Float(32), Int(32) }, 3 };
    Output<Buffer<uint8_t>> static_compiled_buffer_output{ "static_compiled_buffer_output", 3 };
    Output<Buffer<uint8_t>[2]> array_buffer_output{ "array_buffer_output", 3 };

    void generate() {
        simple_output(x, y, c) = cast<float>(simple_input(x, y, c));

        typed_buffer_output(x, y, c) = cast<float>(typed_buffer_input(x, y, c));
        // Note that if we are being invoked via a Stub, "untyped_buffer_output.type()" will
        // assert-fail, because there is no type constraint set: the type
        // will end up as whatever we infer from the values put into it. We'll use an
        // explicit GeneratorParam to allow us to set it.
        untyped_buffer_output(x, y, c) = cast(untyped_buffer_output_type, untyped_buffer_input(x, y, c));

        tupled_output(x, y, c) = Tuple(simple_output(x, y, c), cast<int32_t>(simple_output(x, y, c)) + 1);

        for (int i = 0; i < 2; ++i) {
            array_buffer_output[i](x, y, c) = array_buffer_input[i](x, y,c) + 1 + i;
        }

        // Gratuitous intermediate for the purpose of exercising
        // GeneratorParam<LoopLevel>
        intermediate(x, y, c) = simple_input(x, y, c) * float_arg;

        tuple_output(x, y, c) = Tuple(
                intermediate(x, y, c),
                intermediate(x, y, c) + int_arg[0]);
        // Verify that Output::type() and ::dims() are well-defined after we define the Func
        assert(tuple_output.types()[0] == Float(32));
        assert(tuple_output.types()[1] == Float(32));
        assert(tuple_output.dims() == 3);

        array_output.resize(array_input.size());
        for (size_t i = 0; i < array_input.size(); ++i) {
            array_output[i](x, y) = cast<int16_t>(array_input[i](x, y, 0) + int_arg[i]);
        }

        // This should be compiled into the Generator product itself,
        // and not produce another input for the Stub or AOT filter.
        Buffer<uint8_t> static_compiled_buffer = make_image<uint8_t>(42);
        static_compiled_buffer_output = static_compiled_buffer;
    }

    void schedule() {
        intermediate.compute_at(intermediate_level);
        intermediate.specialize(vectorize).vectorize(x, natural_vector_size<float>());

        typed_buffer_input.dim(2).set_bounds(0, 5);

        // When AOT-compiling, set constraints on the Buffer for this input:
        // - require that we have 7 'channels'
        simple_input.dimensions_and_alignment().dim(2).set_bounds(0, 7);
        // - require that the width is an even multiple of 32
        simple_input.dimensions_and_alignment().dim(0).set_extent(
            simple_input.dimensions_and_alignment().dim(0).extent()/32*32);

        // Ditto for array inputs
        for (size_t i = 0; i < array_input.size(); ++i) {
            array_input.dimensions_and_alignment(i).dim(2).set_bounds(0, (int)(9 + i * 2));
            array_input.dimensions_and_alignment(i).dim(0).set_extent(
                array_input.dimensions_and_alignment(i).dim(0).extent()/32*32);
        }

        // Also set some constraints on Output<Func> for aot-only
        // - require that we have 5 'channels'
        simple_output.dimensions_and_alignment().dim(2).set_bounds(0, 5);
        // - require that the width is an even multiple of 32
        simple_output.dimensions_and_alignment().dim(0).set_extent(
            simple_output.dimensions_and_alignment().dim(0).extent()/32*32);

        // Ditto for array outputs
        for (size_t i = 0; i < array_output.size(); ++i) {
            array_output.dimensions_and_alignment(i).dim(0).set_extent(
                array_output.dimensions_and_alignment(i).dim(0).extent()/32*32);
        }
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};

    Func intermediate{"intermediate"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StubTest, stubtest, StubNS1::StubNS2::StubTest)
