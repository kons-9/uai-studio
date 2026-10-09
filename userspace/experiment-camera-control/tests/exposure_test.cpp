#include "exposure.hpp"
#include <cstdlib>
#include <iostream>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "exposure check failed\n";
        std::exit(1);
    }
}

int main()
{
    experiment::camera::ExposureController controller(3);
    const experiment::camera::Rect content{0, 96, 480, 288};
    const experiment::camera::Rect crop{0, 194, 2592, 1555};
    experiment::camera::Subject subjects[] = {
        {experiment::camera::SubjectKind::kPerson, 990, {0, 96, 400, 280}},
        {experiment::camera::SubjectKind::kFace, 700, {100, 120, 40, 40}}
    };
    experiment::camera::Rect command;
    Require(controller.Propose(UINT32_MAX, subjects, 2, content, crop, command));
    Require(command.x == 540 && command.width == 216 && command.height > 216);
    Require(controller.Propose(0, subjects, 2, content, crop, command));
    controller.Applied(command);
    Require(!controller.Propose(1, subjects, 2, content, crop, command));
    subjects[1].bounds.x += 1;
    Require(!controller.Propose(2, subjects, 2, content, crop, command));
    subjects[1].bounds.x += 80;
    Require(controller.Propose(3, subjects, 2, content, crop, command));
    controller.Applied(command);
    Require(!controller.Propose(4, nullptr, 0, content, crop, command));
    Require(!controller.Propose(4, nullptr, 0, content, crop, command));
    Require(!controller.Propose(3, nullptr, 0, content, crop, command));
    Require(!controller.Propose(5, nullptr, 0, content, crop, command));
    Require(controller.Propose(6, nullptr, 0, content, crop, command));
    Require(command.x == crop.x && command.y == crop.y && command.width == crop.width && command.height == crop.height);
    controller.Applied(command);
    const experiment::camera::Subject padding{experiment::camera::SubjectKind::kFace, 1000, {0, 0, 100, 40}};
    Require(!controller.Propose(7, &padding, 1, content, crop, command));
    const std::uint8_t mask[12]{0, 0, 0, 9, 0, 200, 0, 9, 0, 0, 255, 9};
    experiment::camera::Rect bounds{};
    Require(experiment::camera::MaskBounds(mask, sizeof(mask), 3, 3, 4, 100, bounds));
    Require(bounds.x == 1 && bounds.y == 1 && bounds.width == 2 && bounds.height == 2);
    Require(!experiment::camera::MaskBounds(mask, 10, 3, 3, 4, 100, bounds));
    Require(!experiment::camera::MaskBounds(mask, sizeof(mask), 3, 3, 2, 100, bounds));
    Require(experiment::camera::MaskBounds(mask, sizeof(mask), 3, 3, 4, 255, bounds) && bounds.x == 2 && bounds.y == 2);
}