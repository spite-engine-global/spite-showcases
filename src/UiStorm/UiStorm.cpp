#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "base/ContentRoot.hpp"
#include "base/ServiceLocator.hpp"
#include "control/ControlServer.hpp"
#include "ecs/World.hpp"
#include "engine/Module.hpp"
#include "serialization/JsonFormat.hpp"
#include "ui/UiComponents.hpp"
#include "ui/UiContext.hpp"
#include "ui/UiScope.hpp"
#include "uiview/UiFrame.hpp"
#include "uiview/UiStyleSheet.hpp"
#include "uiview/UiViewContext.hpp"

namespace
{
    using namespace spite;
    constexpr u32 MAX_WIDGETS = 1000000;
    constexpr const char* PATTERNS[] = {"ui-storm.mosaic", "ui-storm.swarm", "ui-storm.orbit"};

    UiRectDesc rect(Vec2 at, Vec2 size)
    {
        UiRectDesc desc;
        desc.offset = at;
        desc.size = size;
        desc.pivot = {0, 0};
        return desc;
    }

    struct Item
    {
        Widget widget;
        Label label;
        u32 kind = 0;
        float feedbackUntil = 0;
    };

    struct Storm
    {
        static constexpr const char* SPITE_SERVICE_NAME = "UiStormSession";
        UiContext* ui;
        UiViewContext* view;
        ControlServer* control;
        InternedString owner;
        std::string world;
        UiScope controls;
        UiScope content;
        Panel backdrop, toolbar;
        NumberField countInput;
        NumberField churnInput;
        Toggle churnToggle;
        Label stats;
        std::vector<u32> feedbackSlots;
        float clock = 0;
        Button pauseButton, patternButton;
        std::vector<Item> items;
        std::vector<u32> order;
        std::minstd_rand random{1977};
        Vec2 surface{1600, 900};
        Vec2 cell{80, 30};
        u32 count = 0, mode = 1, generation = 0, cursor = 0;
        u64 mutations = 0, mixes = 0;
        u64 created = 0, destroyed = 0;
        u32 churnRate = 1000, birthCursor = 0;
        float churnCredit = 0;
        bool recreate = false;
        bool paused = false, autoMix = true, pulsePending = false;
        float elapsed = 0, mixTime = 0, pulseTime = 0;
        u32 frames = 0;
        float fps = 0;
        u32 uiEntities = 0;
        World game;

        Storm(const ModuleContext& context, World value) :
            ui(&value.services().get<UiContext>()),
            view(value.services().contains<UiViewContext>() ? &value.services().get<UiViewContext>() : nullptr),
            control(&context.control), owner(context.owner),
            world(&value.services() == &context.scene.services() ? "scene" : "play"), controls(*ui), content(*ui),
            game(value)
        {
            if (view)
            {
                (void)view->registerFont("JetBrainsMono",
                                         resolveContentPath("assets/fonts/JetBrainsMonoRegularFont.ttf"));
                (void)view->stylesheet().load(resolveContentPath("assets/ui/engine.json"));

                for (const auto& [font, file] :
                     {std::pair{"ChakraPetch", "ChakraPetch-Bold.ttf"}, std::pair{"Geist", "Geist-Regular.ttf"},
                      std::pair{"GeistMono", "GeistMono-Regular.ttf"},
                      std::pair{"GeistMonoMedium", "GeistMono-Medium.ttf"}})
                    (void)(*view).registerFont(font, resolveContentPath(std::format("assets/fonts/{}", file)));
                const auto forgeTheme = resolveContentPath("assets/ui/themes/forge-dark.json");
                (void)(*view).stylesheet().setTheme("forge-dark", std::span(&forgeTheme, 1));
                (void)view->stylesheet().loadLayer("ui-storm", resolveContentPath("assets/ui/ui-storm.json"));
            }
            backdrop = controls.createPanel("storm.background", rect({}, surface));
            backdrop.setStyleClass("storm.background");
            backdrop.setInteractable(false);
            backdrop.setZOffset(-100);
            toolbar = controls.createPanel("storm.toolbar", rect({12, 12}, {1480, 136}));
            toolbar.setStyleClass("storm.toolbar");
            toolbar.setZOffset(10000);
            auto title = controls.createLabel("storm.title", "", rect({14, 6}, {170, 35}));
            title.setParent(toolbar);
            title.setStyleClass("storm.title");
            title.setLocalizedText("ui-storm.title");
            countInput = controls.createNumberField("storm.count",
                                                    {.rect = rect({190, 8}, {110, 32}),
                                                     .value = 2000,
                                                     .min = 1,
                                                     .max = MAX_WIDGETS,
                                                     .step = 100,
                                                     .decimals = 0});
            countInput.setParent(toolbar);
            countInput.setStyleClass("storm.input");
            countInput.setAccessibleNameKey("ui-storm.count");
            button("storm.build", "ui-storm.build", 310, 92, [this] { buildRequest(u32(countInput.number())); });
            button("storm.gallery", "ui-storm.gallery", 412, 112, [this] { buildRequest(256); });
            button("storm.storm", "ui-storm.storm", 534, 112, [this] { buildRequest(10000); });
            button("storm.overload", "ui-storm.overload", 656, 128, [this] { buildRequest(50000); });
            button("storm.mix", "ui-storm.mix", 794, 92, [this] { request("uiStormMix"); });
            button("storm.styles", "ui-storm.styles", 896, 92, [this] { request("uiStormStyles"); });
            pauseButton = button("storm.pause", "ui-storm.pause", 998, 92, [this] { request("uiStormPause"); });
            patternButton = button("storm.pattern", PATTERNS[mode], 1100, 112, [this] { request("uiStormPattern"); });
            auto automatic = controls.createToggle("storm.auto", "", rect({1226, 8}, {210, 32}), true);
            automatic.setParent(toolbar);
            automatic.setAccessibleNameKey("ui-storm.automatic");
            automatic.onChange([this](bool) { request("uiStormAuto"); });
            auto autoText = controls.createLabel("storm.autoText", "", rect({1280, 8}, {170, 32}));
            autoText.setParent(toolbar);
            autoText.setStyleClass("storm.text");
            autoText.setLocalizedText("ui-storm.automatic");
            autoText.setInteractable(false);
            stats = controls.createLabel("storm.stats", "", rect({14, 49}, {1440, 34}));
            stats.setParent(toolbar);
            stats.setStyleClass("storm.text");
            stats.setLocalizedText("ui-storm.stats");
            churnToggle = controls.createToggle("storm.recreate", "", rect({14, 92}, {52, 30}), false);
            churnToggle.setParent(toolbar);
            churnToggle.setAccessibleNameKey("ui-storm.recreate");
            churnToggle.onChange(
                [this](bool value)
                {
                    control->runAtFrameStart(
                        owner.toView(),
                        std::format(R"({{"command":"uiStormRecreate","args":{{"enabled":{},"world":"{}"}}}})", value,
                                    world),
                        [](const std::string&) {});
                });
            auto churnLabel = controls.createLabel("storm.recreateLabel", "", rect({78, 91}, {260, 32}));
            churnLabel.setParent(toolbar);
            churnLabel.setStyleClass("storm.text");
            churnLabel.setLocalizedText("ui-storm.recreate");
            churnLabel.setInteractable(false);
            churnInput = controls.createNumberField("storm.churnRate",
                                                    {.rect = rect({345, 91}, {110, 32}),
                                                     .value = double(churnRate),
                                                     .min = 1,
                                                     .max = 10000,
                                                     .step = 100,
                                                     .decimals = 0});
            churnInput.setParent(toolbar);
            churnInput.setStyleClass("storm.input");
            churnInput.setAccessibleNameKey("ui-storm.rate");
            churnInput.onNumber(
                [this](double value)
                {
                    control->runAtFrameStart(
                        owner.toView(),
                        std::format(R"({{"command":"uiStormRecreate","args":{{"rate":{},"world":"{}"}}}})", u32(value),
                                    world),
                        [](const std::string&) {});
                });
            auto rateLabel = controls.createLabel("storm.rateLabel", "", rect({468, 91}, {950, 32}));
            rateLabel.setParent(toolbar);
            rateLabel.setStyleClass("storm.text");
            rateLabel.setLocalizedText("ui-storm.rate");
            rateLabel.setInteractable(false);
            controls.commit();
        }

        Button button(const char* id, const char* key, float x, float width, std::function<void()> fn)
        {
            auto result = controls.createButton(id, "", rect({x, 8}, {width, 32}));
            result.setParent(toolbar);
            result.setStyleClass("storm.control");
            result.setLocalizedText(key);
            result.onClick(std::move(fn));
            return result;
        }

        void request(const char* command)
        {
            control->runAtFrameStart(owner.toView(),
                                     std::format(R"({{"command":"{}","args":{{"world":"{}"}}}})", command, world),
                                     [](const std::string&) {});
        }

        void buildRequest(u32 amount)
        {
            control->runAtFrameStart(
                owner.toView(),
                std::format(R"({{"command":"uiStormBuild","args":{{"count":{},"world":"{}"}}}})", amount, world),
                [](const std::string&) {});
        }

        Vec2 position(u32 slot) const
        {
            if (mode == 2)
            {
                const float angle = float(slot) * 2.39996323f + float(mixes) * .8f;
                const float radius = std::sqrt((float(slot) + .5f) / float(count));
                return {surface.x * .5f + std::cos(angle) * radius * (surface.x - cell.x) * .46f,
                        160 + (surface.y - 160) * .5f + std::sin(angle) * radius * (surface.y - 170 - cell.y) * .46f};
            }
            const u32 columns = std::max(1u, u32(surface.x / cell.x));
            return {float(slot % columns) * cell.x + 4, 160 + float(slot / columns) * cell.y};
        }

        void animate(u32 i)
        {
            auto& item = items[i];
            if (item.feedbackUntil > clock)
                return;
            Vec2 destination = position(order[i]);
            if (mode == 1)
                destination = {float(random() % 10000) / 10000 * std::max(1.f, surface.x - cell.x),
                               160 + float(random() % 10000) / 10000 * std::max(1.f, surface.y - 165 - cell.y)};
            UiTweenDesc motion;
            motion.element = item.widget.entity();
            motion.component = "UiRect";
            motion.field = "offset";
            motion.to = {destination.x, destination.y, 0, 0};
            motion.duration = .7f + float(i % 17) * .09f;
            motion.ease = i % 3 == 0 ? UiEase::OutBack : UiEase::InOutCubic;
            motion.repeat = 0;
            motion.yoyo = true;
            (void)ui->tweens().start(motion);
            UiTweenDesc paint;
            paint.element = item.widget.entity();
            paint.component = "UiVisual";
            paint.field = i % 2 ? "opacity" : "scale";
            paint.from = i % 2 ? Vec4{1, 0, 0, 0} : Vec4{1, 1, 0, 0};
            paint.to = i % 2 ? Vec4{.28f, 0, 0, 0} : Vec4{.65f, 1.25f, 0, 0};
            paint.duration = .6f + float(i % 19) * .13f;
            paint.repeat = 0;
            paint.yoyo = true;
            paint.ease = UiEase::InOutQuad;
            (void)ui->tweens().start(paint);
        }

        void buttonFeedback(u32 slot, Entity entity, bool clicked)
        {
            if (slot >= items.size() || items[slot].widget.entity() != entity)
                return;
            auto& item = items[slot];
            if (item.feedbackUntil == 0)
                feedbackSlots.push_back(slot);
            item.feedbackUntil = clock + (clicked ? 1.0f : 3.0f);
            ui->tweens().cancelElement(entity);
            if (game.hasComponent<UiVisualComponent>(entity))
                game.getComponent<UiVisualComponent>(entity) = UiVisualComponent{};
            auto button = item.widget.as<Button>();
            button.setStyleClass(clicked ? "storm.clicked" : "storm.pressed");
        }

        void finishFeedback()
        {
            std::erase_if(feedbackSlots,
                          [this](u32 slot)
                          {
                              if (slot >= items.size())
                                  return true;
                              auto& item = items[slot];
                              if (item.feedbackUntil > clock)
                                  return false;
                              item.feedbackUntil = 0;
                              item.widget.setStyleClass(InternedString(std::format("storm.{}", slot % 24)));
                              item.widget.as<Button>().setLocalizedText("ui-storm.tile");
                              if (!paused)
                                  animate(slot);
                              return true;
                          });
        }

        Item createItem(u32 i)
        {
            Item item;
            item.kind = (i + generation) % 9;
            const auto id = InternedString(std::format("storm.item.{}", i));
            const auto desc = rect(position(i), {std::max(2.f, cell.x - 3), std::max(2.f, cell.y - 3)});
            switch (item.kind)
            {
            case 0:
                item.widget = content.createButton(id, "", desc);
                item.widget.as<Button>().setLocalizedText("ui-storm.tile");
                item.widget.onPress(
                    [this, i, entity = item.widget.entity()](u8 button, Vec2)
                    {
                        if (button == 0)
                            buttonFeedback(i, entity, false);
                    });
                item.widget.onClick([this, i, entity = item.widget.entity()] { buttonFeedback(i, entity, true); });
                break;
            case 1:
                item.label = content.createLabel(id, "", desc);
                item.label.setLocalizedText("ui-storm.item");
                item.label.setLocalizedArg("n", i);
                item.widget = item.label;
                break;
            case 2:
                item.widget = content.createTextInput(id, desc);
                item.widget.as<TextInput>().setText(std::to_string(i));
                item.widget.setAccessibleNameKey("ui-storm.sampleInput");
                break;
            case 3:
                item.widget =
                    content.createNumberField(id, {.rect = desc, .value = double(i), .min = 0, .max = 100000});
                item.widget.setAccessibleNameKey("ui-storm.sampleNumber");
                break;
            case 4:
                item.widget = content.createCheckbox(id, "", desc, i % 2 == 0);
                item.widget.setAccessibleNameKey("ui-storm.sampleCheck");
                break;
            case 5:
                item.widget = content.createToggle(id, "", desc, i % 3 == 0);
                item.widget.setAccessibleNameKey("ui-storm.sampleToggle");
                break;
            case 6:
                item.widget = content.createSlider(id, {.rect = desc, .value = double(i % 100) / 100});
                item.widget.setAccessibleNameKey("ui-storm.sampleSlider");
                break;
            case 7:
                item.widget = content.createProgressBar(id, {.rect = desc, .value = double(i % 100) / 100});
                item.widget.setAccessibleNameKey("ui-storm.sampleProgress");
                break;
            default:
                item.widget = content.createPanel(id, desc);
                item.label =
                    content.createLabel(InternedString(std::format("storm.item.{}.label", i)), "", rect({}, desc.size));
                item.label.setParent(item.widget);
                item.label.setLocalizedText("ui-storm.item");
                item.label.setLocalizedArg("n", i);
                break;
            }
            item.widget.setStyleClass(InternedString(std::format("storm.{}", i % 24)));
            if (item.kind == 8)
                item.label.setStyleClass(InternedString(std::format("storm.{}", i % 24)));
            if (item.kind == 1 || item.kind == 7 || item.kind == 8)
                item.widget.setInteractable(false);
            return item;
        }

        void build(u32 amount)
        {
            destroyed += items.size();
            feedbackSlots.clear();
            items.clear();
            content.clear();
            const u32 baseline = game.activeEntityCount();
            count = amount;
            created += count;
            churnCredit = 0;
            ++generation;
            paused = false;
            pauseButton.setLocalizedText("ui-storm.pause");
            countInput.setNumber(count);
            if (view && view->frame().targetSize.x > 0)
                surface = view->frame().targetSize;
            backdrop.setSize(surface);
            const float areaHeight = std::max(100.f, surface.y - 170);
            const u32 columns = std::max(1u, u32(std::ceil(std::sqrt(float(count) * surface.x / areaHeight * .42f))));
            cell = {surface.x / float(columns), areaHeight / float((count + columns - 1) / columns)};
            order.resize(count);
            std::iota(order.begin(), order.end(), 0);
            items.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                items.push_back(createItem(i));
                if (sceneLoading(game) && (i + 1) % 512 == 0)
                {
                    content.commit();
                    sceneLoadCheckpoint(game, "showcase.loadWidgets", i + 1, count);
                }
            }
            content.commit();
            uiEntities = game.activeEntityCount() - baseline;
            for (u32 i = 0; i < count; ++i)
            {
                animate(i);
                if ((i + 1) % 1024 == 0)
                    sceneLoadCheckpoint(game, "showcase.loadWidgets", i + 1, count);
            }
            mixTime = 0;
            report();
        }

        void mix()
        {
            ++mixes;
            std::shuffle(order.begin(), order.end(), random);
            if (!paused)
                for (u32 i = 0; i < count; ++i)
                    animate(i);
            else
                for (u32 i = 0; i < count; ++i)
                    items[i].widget.setOffset(position(order[i]));
        }

        void styles()
        {
            for (auto& item : items)
                if (item.feedbackUntil <= clock)
                    item.widget.setStyleClass(InternedString(std::format("storm.{}", random() % 24)));
            mutations += count;
        }

        void replaceLive()
        {
            const u32 amount = std::min({u32(churnCredit), count, 5000u});
            if (!amount)
                return;
            churnCredit -= float(amount);
            const u32 before = game.activeEntityCount();
            std::vector<u32> slots;
            slots.reserve(amount);
            for (u32 i = 0; i < amount; ++i)
            {
                const u32 slot = birthCursor++ % count;
                auto& item = items[slot];
                if (item.feedbackUntil > clock)
                    continue;
                ui->tweens().cancelElement(item.widget.entity());
                if (item.kind == 8)
                    content.destroy(item.label);
                content.destroy(item.widget);
                slots.push_back(slot);
            }
             
            content.commit();
            ++generation;
            for (u32 slot : slots)
                items[slot] = createItem(slot);
            content.commit();
            for (u32 slot : slots)
                animate(slot);
            uiEntities = uiEntities + game.activeEntityCount() - before;
            created += slots.size();
            destroyed += slots.size();
        }

        void pulse()
        {
            pulsePending = false;
            finishFeedback();
            if (paused || !count)
                return;
            if (recreate)
                replaceLive();
            const u32 changes = std::min(count, std::max(12u, count / 40));
            for (u32 n = 0; n < changes; ++n)
            {
                auto& item = items[cursor++ % count];
                if (item.feedbackUntil > clock)
                    continue;
                const double value = double(random() % 100) / 100;
                item.widget.setStyleClass(InternedString(std::format("storm.{}", random() % 24)));
                if (item.label.isValid())
                    item.label.setLocalizedArg("n", mutations + n);
                switch (item.kind)
                {
                case 0:
                    break;
                case 2:
                    item.widget.as<TextInput>().setText(std::to_string(random() % 100000));
                    break;
                case 3:
                    item.widget.as<NumberField>().setNumber(random() % 100000);
                    break;
                case 4:
                    item.widget.as<Checkbox>().setChecked(value > .5);
                    break;
                case 5:
                    item.widget.as<Toggle>().setChecked(value > .5);
                    break;
                case 6:
                    item.widget.as<Slider>().setValue(value);
                    break;
                case 7:
                    item.widget.as<ProgressBar>().setValue(value);
                    break;
                default:
                    break;
                }
            }
            mutations += changes;
            report();
        }

        void report()
        {
            stats.setLocalizedArg("count", count);
            stats.setLocalizedArg("entities", uiEntities);
            stats.setLocalizedArg("tweens", ui->tweens().activeCount());
            stats.setLocalizedArg("fps", u32(std::max(0.f, fps)));
            stats.setLocalizedArg("changes", mutations);
            stats.setLocalizedArg("mixes", mixes);
            stats.setLocalizedArg("created", created);
            stats.setLocalizedArg("destroyed", destroyed);
        }
    };

    void drive(SystemContext& context)
    {
        auto storm = context.services().write<Storm>();
        storm->clock += context.deltaTime;
        storm->elapsed += context.deltaTime;
        ++storm->frames;
        if (storm->elapsed >= .5f)
        {
            storm->fps = float(storm->frames) / storm->elapsed;
            storm->frames = 0;
            storm->elapsed = 0;
        }
        storm->pulseTime += context.deltaTime;
        if (storm->recreate && !storm->paused)
            storm->churnCredit = std::min(storm->churnCredit + context.deltaTime * storm->churnRate, 10000.f);
        if (storm->pulseTime >= .15f && !storm->pulsePending)
        {
            storm->pulseTime = 0;
            storm->pulsePending = true;
            storm->request("uiStormPulse");
        }
        if (!storm->paused && storm->autoMix)
        {
            storm->mixTime += context.deltaTime;
            if (storm->mixTime >= 2.8f)
            {
                storm->mixTime = 0;
                storm->request("uiStormMix");
            }
        }
    }

    const World* target(ControlServer& control, const ControlArgs& args)
    {
        const auto world = args.string("world") ? control.world(args) : control.world("play");
        return world && world->services().contains<Storm>() ? world : nullptr;
    }

    bool load(const ModuleContext& context)
    {
        auto& control = context.control;
        control.registerCommand(
            "uiStormRecreate", context.owner.toView(),
            "configure live destruction and replacement at a target rate of 1..10000 widgets per second",
            {optionalArg("enabled", ArgKind::Boolean, "enable or disable live replacement"),
             optionalArg("rate", ArgKind::Integer, "target replacements per second, 1..10000"), WORLD_ARG},
            [&control](const ControlArgs& args, IFormatWriter& out) -> std::string
            {
                const auto world = target(control, args);
                if (!world)
                    return "UI Storm needs a Play world";
                const auto rate = args.integer("rate");
                if (rate && (*rate == 0 || *rate > 10000))
                    return "rate must be 1..10000";
                auto s = world->services().write<Storm>();
                if (rate)
                    s->churnRate = u32(*rate);
                if (const auto enabled = args.boolean("enabled"))
                    s->recreate = *enabled;
                s->churnCredit = 0;
                s->churnToggle.setChecked(s->recreate);
                s->churnInput.setNumber(s->churnRate);
                out.writeBool("enabled", s->recreate);
                out.writeU32("rate", s->churnRate);
                return {};
            });
        control.registerCommand(
            "uiStormBuild", context.owner.toView(), "rebuild 1..1000000 mixed widgets with two tweens per widget",
            {requiredArg("count", ArgKind::Integer, "widget roots (internal widget parts add entities)"), WORLD_ARG},
            [&control](const ControlArgs& args, IFormatWriter& out) -> std::string
            {
                const auto world = target(control, args);
                if (!world)
                    return "UI Storm needs a Play world";
                const auto amount = args.integer("count");
                if (!amount || *amount == 0 || *amount > MAX_WIDGETS)
                    return "count must be 1..1000000";
                world->services().get<Storm>().build(u32(*amount));
                out.writeU32("widgets", u32(*amount));
                return {};
            });
        const auto command = [&](const char* name, const char* doc, void (*action)(Storm&))
        {
            control.registerCommand(name, context.owner.toView(), doc, {WORLD_ARG},
                                    [&control, action](const ControlArgs& args, IFormatWriter&) -> std::string
                                    {
                                        const auto world = target(control, args);
                                        if (!world)
                                            return "UI Storm needs a Play world";
                                        auto storm = world->services().write<Storm>();
                                        action(*storm);
                                        return {};
                                    });
        };
        command("uiStormMix", "shuffle every widget and retarget its motion", [](Storm& s) { s.mix(); });
        command("uiStormStyles", "randomize all widget style families", [](Storm& s) { s.styles(); });
        command("uiStormPulse", "change a batch of texts, values, styles and enabled states",
                [](Storm& s) { s.pulse(); });
        command("uiStormAuto", "toggle automatic shuffling", [](Storm& s) { s.autoMix = !s.autoMix; });
        command("uiStormPattern", "cycle mosaic, swarm and orbit layouts",
                [](Storm& s)
                {
                    s.mode = (s.mode + 1) % 3;
                    s.patternButton.setLocalizedText(PATTERNS[s.mode]);
                    s.mix();
                });
        command("uiStormPause", "stop or restart benchmark motion and churn",
                [](Storm& s)
                {
                    s.paused = !s.paused;
                    s.pauseButton.setLocalizedText(s.paused ? "ui-storm.resume" : "ui-storm.pause");
                    for (u32 i = 0; i < s.count; ++i)
                        if (s.paused)
                            s.ui->tweens().cancelElement(s.items[i].widget.entity());
                        else
                            s.animate(i);
                    s.report();
                });
        control.registerQuery("uiStormStatus", context.owner.toView(),
                              "live workload counts; FPS is observed frame cadence, not a perf capture", {WORLD_ARG},
                              [&control](const ControlArgs& args, IFormatWriter& out) -> std::string
                              {
                                  const auto world = target(control, args);
                                  if (!world)
                                      return "UI Storm needs a Play world";
                                  const auto s = world->services().read<Storm>();
                                  out.writeU32("widgets", s->count);
                                  out.writeU32("uiEntities", s->uiEntities);
                                  out.writeU32("activeTweens", s->ui->tweens().activeCount());
                                  out.writeU64("mutations", s->mutations);
                                  out.writeU64("mixes", s->mixes);
                                  out.writeU64("created", s->created);
                                  out.writeU64("destroyed", s->destroyed);
                                  out.writeBool("recreate", s->recreate);
                                  out.writeU32("replacementsPerSecond", s->churnRate);
                                  out.writeBool("paused", s->paused);
                                  out.writeBool("autoMix", s->autoMix);
                                  out.writeU32("pattern", s->mode);
                                  out.writeF32("fps", s->fps);
                                  return {};
                              });
        return true;
    }

    void attach(const ModuleContext& context, World world)
    {
        if (!world.services().contains<UiContext>())
            return;
        auto storm = std::make_shared<Storm>(context, world);
        world.services().registerService(storm, context.owner);
        world.registerSystem({.moduleName = context.owner.toView().data(),
                              .name = "UiStormDrive",
                              .fn = &drive,
                              .update = SystemPhaseDescriptor{}});
        if (!sceneLoading(world))
            storm->buildRequest(2000);
    }
}  

#ifdef SPITE_SHOWCASE_BUNDLE
extern "C" const spite::ModuleDescriptor* spiteUiStorm()
#else
SPITE_MODULE_EXPORT const spite::ModuleDescriptor* SPITE_MODULE_ENTRY()
#endif
{
    static const spite::ModuleDescriptor descriptor{.name = "UiStorm", .load = &load, .attachWorld = &attach};
    return &descriptor;
}
