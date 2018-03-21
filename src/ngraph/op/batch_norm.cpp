/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "ngraph/op/batch_norm.hpp"
#include "ngraph/op/constant.hpp"
#include "ngraph/op/get_output_element.hpp"

using namespace std;
using namespace ngraph;

op::BatchNorm::BatchNorm(double eps,
                                 shared_ptr<Node> gamma,
                                 shared_ptr<Node> beta,
                                 shared_ptr<Node> input)
    : RequiresTensorViewArgs("BatchNorm", {gamma, beta, input})
    , m_bn_input_shape(input->get_shape())
    , m_epsilon(eps)
{
    if (m_bn_input_shape.size() < 2)
    {
        throw ngraph_error("input tensor to batchnorm much have tensor of atleast rank 2");
    }
    else
    {
        this->m_bn_variance_shape.push_back(input->get_shape()[1]);
        this->m_bn_mean_shape.push_back(input->get_shape()[1]);
    }

    if (m_bn_input_shape[1] == 0)
    {
        throw ngraph_error(
            "input tensor must have atleast one channel axis for batch normalization");
    }

    if ((gamma->get_shape().size() != 1) || (beta->get_shape().size() != 1))
    {
        throw ngraph_error("gamma and beta shoud have rank 1");
    }

    if (gamma->get_shape().size() != beta->get_shape().size())
    {
        throw ngraph_error("gamma and beta rank does not match");
    }

    if (gamma->get_element_type() != beta->get_element_type())
    {
        throw ngraph_error("gamma and beta element type does not match");
    }

    add_output(input->get_element_type(), m_bn_input_shape);
    add_output(input->get_element_type(), m_bn_mean_shape);
    add_output(input->get_element_type(), m_bn_variance_shape);
}

shared_ptr<Node>
    op::BatchNorm::copy_with_new_args(const NodeVector& new_args) const
{
    if (new_args.size() != 3)
        throw ngraph_error("Incorrect number of new arguments");
    return make_shared<BatchNorm>(m_epsilon, new_args.at(0), new_args.at(1), new_args.at(2));
}

op::BatchNormBackprop::BatchNormBackprop(double eps,
                                                 shared_ptr<Node> gamma,
                                                 shared_ptr<Node> beta,
                                                 shared_ptr<Node> input,
                                                 shared_ptr<Node> mean,
                                                 shared_ptr<Node> variance,
                                                 shared_ptr<Node> delta)
    : RequiresTensorViewArgs("BatchNormBackprop", {gamma, beta, input, mean, variance, delta})
    , epsilon(eps)

{
    if (input->get_shape().size() != 4)
    {
        throw ngraph_error("Input expected to be a 4D tensor");
    }

    auto et = input->get_element_type();
    const char* input_names[] = {"gamma", "beta", "input", "mean", "variance", "delta"};

    for (size_t i = 0; i < get_input_size(); i++)
    {
        if (get_input_op(i)->get_element_type() != et)
        {
            auto err_msg = string("The element type of ") + input_names[i] +
                           " isn't equal to input data's type";
            throw ngraph_error(err_msg.c_str());
        }
    }

    Shape channel_shape{input->get_shape().at(1)};

    for (size_t i = 0; i < get_input_size(); i++)
    {
        if (i == 2 || i == 5) //don't check input and delta
        {
            continue;
        }

        if (get_input_op(i)->get_shape() != channel_shape)
        {
            auto err_msg = string("The shape of ") + input_names[i] +
                           " isn't equal to input channel's shape";
            throw ngraph_error(err_msg.c_str());
        }
    }

    if (delta->get_shape() != input->get_shape())
    {
        throw ngraph_error("delta shape is expected to be equal to input shape");
    }

    add_output(input->get_element_type(), input->get_shape());
    add_output(gamma->get_element_type(), gamma->get_shape());
    add_output(beta->get_element_type(), beta->get_shape());
}

shared_ptr<Node>
    op::BatchNormBackprop::copy_with_new_args(const NodeVector& new_args) const
{
    if (new_args.size() != 6)
    {
        throw ngraph_error("Incorrect number of new arguments");
    }
    return make_shared<op::BatchNormBackprop>(epsilon,
                                                   new_args.at(0),
                                                   new_args.at(1),
                                                   new_args.at(2),
                                                   new_args.at(3),
                                                   new_args.at(4),
                                                   new_args.at(5));
}

void op::BatchNorm::generate_adjoints(autodiff::Adjoints& adjoints,
                                              const shared_ptr<Node>& delta)
{
    auto gamma = get_input_op(0);
    auto beta = get_input_op(1);
    auto input = get_input_op(2);

    //Extract mean and variance outputs from BatchNorm
    //as these are used by BatchNormBackprop.
    //The users of the outputs (GetOutputElements' Inputs) aren't sorted
    //and get_n() is used to sort the inputs in the same order as Batchnorm's outputs
    //Next, Mean and Variance (`at(1)` and `at(2)`) are extracted
    //Please see `add_output` in `BatchNorm::BatchNorm` for more details
    vector<shared_ptr<Node>> goes(get_outputs().size());

    for (auto _input : get_output_inputs(0))
    {
        auto goe = dynamic_pointer_cast<op::GetOutputElement>(_input->get_node());
        goes.at(goe->get_n()) = _input->get_node();
    }

    auto mean = goes.at(1);
    auto var = goes.at(2);
    auto bbn = make_shared<op::BatchNormBackprop>(
        get_eps_value(), gamma, beta, input, mean, var, delta);
    auto dinput = make_shared<op::GetOutputElement>(bbn, 0);
    auto dgamma = make_shared<op::GetOutputElement>(bbn, 1);
    auto dbeta = make_shared<op::GetOutputElement>(bbn, 2);

    adjoints.add_delta(input, dinput);
    adjoints.add_delta(gamma, dgamma);
    adjoints.add_delta(beta, dbeta);
}
