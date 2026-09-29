// PetBar — a menu bar companion for codex-pet-bridge.
//
// Shows what Codex / Claude Code / Hermes / OpenClaw are doing, the board's
// telemetry, and lets you switch the board's page, clock face and play the
// easter egg. It only talks to the bridge (default http://127.0.0.1:17366),
// with the token from ~/.codex-pet-bridge/token.
//
// Build: ./build.sh   (needs the Xcode Command Line Tools; no Xcode project)

import AppKit
import Foundation
import ServiceManagement

// MARK: - Bridge data (GET /status)

struct StatusResponse: Decodable {
    let serverTimeMs: Double?
    let bridge: BridgeInfo?
    let device: DeviceInfo?
    let current: CurrentInfo?
    let agents: [AgentInfo]?
    let usage: UsageInfo?
    let settings: SettingsInfo?
    let animations: [AnimationInfo]?
    let egg: EggInfo?
    let realtime: RealtimeInfo?
}

struct BridgeInfo: Decodable {
    let version: String?
    let uptimeS: Double?
    let tokenRequired: Bool?
}

struct DeviceInfo: Decodable {
    let seen: Bool?
    let online: Bool?
    let lastSeenAt: String?
    let firmware: String?
    let battery: Int?
    let charging: Bool?
    let temperatureC: Double?
    let humidity: Double?
    let rssi: Int?
    let page: String?
    let clockStyle: String?
}

struct CurrentInfo: Decodable {
    let source: String?
    let task: String?
    let status: String?
    let time: String?
}

struct AgentInfo: Decodable {
    let id: String?
    let source: String?
    let task: String?
    let status: String?
    let updatedAt: String?
    let family: String?
    let project: String?

    enum CodingKeys: String, CodingKey {
        case id, source, task, status, family, project
        case updatedAt = "updated_at"
    }
}

struct UsageInfo: Decodable {
    let today: String?
    let todayLabel: String?
    let context: String?
    let contextLabel: String?
    let quota: String?
    let quotaLabel: String?

    enum CodingKeys: String, CodingKey {
        case today, context, quota
        case todayLabel = "today_label"
        case contextLabel = "context_label"
        case quotaLabel = "quota_label"
    }
}

struct SettingsInfo: Decodable {
    let clockStyle: String?
    let hour12: Bool?
    let showSeconds: Bool?
    let defaultAnimation: String?
}

struct AnimationInfo: Decodable {
    let id: String
    let frames: Int?
    let durationS: Double?
    let width: Int?
    let height: Int?
}

struct EggInfo: Decodable {
    let playing: Bool?
    let id: String?
    let phase: String?
}

struct RealtimeInfo: Decodable {
    let claude: ClaudeRealtime?
}

struct ClaudeRealtime: Decodable {
    let tool: String?
    let model: String?
    let project: String?
}

// MARK: - Labels

enum Tone {
    case working, attention, done, error, idle
}

let statusLabels: [String: (String, Tone)] = [
    "running": ("工作中", .working),
    "working": ("工作中", .working),
    "started": ("开始了", .working),
    "progress": ("进行中", .working),
    "near-complete": ("快完成了", .working),
    "thinking": ("思考中", .working),
    "searching": ("搜索中", .working),
    "tool-use": ("调用工具", .working),
    "needs-attention": ("等你处理", .attention),
    "completed": ("完成", .done),
    "error": ("出错", .error),
    "failed": ("出错", .error),
    "idle": ("空闲", .idle)
]

func statusLabel(_ status: String?) -> (String, Tone) {
    guard let status = status, !status.isEmpty else { return ("空闲", .idle) }
    return statusLabels[status] ?? (status, .idle)
}

func familyOf(_ source: String?) -> String {
    let text = (source ?? "").lowercased()
    if text.contains("claude") { return "claude" }
    if text.contains("codex") { return "codex" }
    if text.contains("hermes") { return "hermes" }
    if text.contains("openclaw") { return "openclaw" }
    return ""
}

func familyName(_ family: String, fallback: String?) -> String {
    switch family {
    case "claude": return "Claude Code"
    case "codex": return "Codex"
    case "hermes": return "Hermes"
    case "openclaw": return "OpenClaw"
    default: return fallback ?? "智能体"
    }
}

func prettyTask(_ task: String?) -> String {
    guard let task = task, !task.isEmpty else { return "" }
    for suffix in ["codex-runtime", "claude-session", "agent-task"] where task.hasSuffix(suffix) {
        return ""
    }
    return task
}

let clockFaces: [(String, String)] = [
    ("sans", "大字"),
    ("segment", "七段数码"),
    ("dots", "点阵"),
    ("analog", "指针表盘"),
    ("words", "英文字钟"),
    ("terminal", "终端"),
    ("pet", "宠物")
]

let boardPages: [(String, String)] = [
    ("overview", "概览"),
    ("usage", "用量"),
    ("clock", "时钟")
]

// MARK: - Time

let isoWithFraction: ISO8601DateFormatter = {
    let formatter = ISO8601DateFormatter()
    formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return formatter
}()

let isoPlain: ISO8601DateFormatter = {
    let formatter = ISO8601DateFormatter()
    formatter.formatOptions = [.withInternetDateTime]
    return formatter
}()

func parseDate(_ text: String?) -> Date? {
    guard let text = text, !text.isEmpty else { return nil }
    return isoWithFraction.date(from: text) ?? isoPlain.date(from: text)
}

func timeAgo(_ text: String?, now: Date) -> String {
    guard let date = parseDate(text) else { return "" }
    let seconds = max(0, Int(now.timeIntervalSince(date).rounded()))
    if seconds < 5 { return "刚刚" }
    if seconds < 60 { return "\(seconds) 秒前" }
    if seconds < 3600 { return "\(seconds / 60) 分钟前" }
    if seconds < 86400 { return "\(seconds / 3600) 小时前" }
    return "\(seconds / 86400) 天前"
}

// MARK: - Icons

enum PetIcon {
    // 15 x 11 cells; "X" is ink. The pet is the same one as the dashboard icon.
    static let body = [
        "...............",
        "....XXXX.......",
        "..XXXXXXXX.....",
        ".XXXXXXXXXX....",
        ".XX.XXXX.XX....",
        "XXX.XXXX.XXX...",
        "XXXXXXXXXXXX...",
        "XXXXX..XXXXX...",
        "XXXXXXXXXXXX...",
        ".XXXXXXXXXX....",
        ".XX..XX..XX...."
    ]

    static func cells(for tone: Tone?) -> [[Bool]] {
        var grid = body.map { row in row.map { $0 == "X" } }
        func set(_ row: Int, _ col: Int, _ value: Bool) { grid[row][col] = value }
        switch tone {
        case .some(.working):
            // squinting eyes and a little spark: busy
            set(4, 3, true)
            set(4, 8, true)
            set(0, 13, true)
            set(1, 12, true)
            set(1, 14, true)
            set(2, 13, true)
        case .some(.attention):
            // exclamation mark: someone is waiting for you
            for row in 0...3 { set(row, 13, true) }
            set(5, 13, true)
        case .some(.done):
            // check mark
            set(1, 11, true)
            set(2, 12, true)
            set(1, 13, true)
            set(0, 14, true)
        case .some(.error):
            // x eyes
            set(4, 3, true)
            set(5, 3, true)
            set(4, 8, true)
            set(5, 8, true)
            set(0, 12, true)
            set(1, 13, true)
            set(0, 14, true)
            set(2, 12, true)
            set(2, 14, true)
        default:
            break
        }
        return grid
    }

    static func image(tone: Tone?, offline: Bool) -> NSImage {
        let grid = cells(for: tone)
        let cell: CGFloat = 1.5
        let size = NSSize(width: CGFloat(grid[0].count) * cell, height: CGFloat(grid.count) * cell + 1)
        let image = NSImage(size: size, flipped: true) { _ in
            NSColor.black.withAlphaComponent(offline ? 0.35 : 1).setFill()
            for (row, cells) in grid.enumerated() {
                for (col, ink) in cells.enumerated() where ink {
                    NSRect(x: CGFloat(col) * cell, y: CGFloat(row) * cell + 0.5, width: cell, height: cell).fill()
                }
            }
            return true
        }
        image.isTemplate = true
        return image
    }

    static func dot(_ tone: Tone) -> NSImage {
        let color: NSColor
        switch tone {
        case .working: color = NSColor.systemBlue
        case .attention: color = NSColor.systemOrange
        case .done: color = NSColor.systemGreen
        case .error: color = NSColor.systemPink
        case .idle: color = NSColor.systemGray
        }
        let image = NSImage(size: NSSize(width: 10, height: 10), flipped: false) { rect in
            color.setFill()
            NSBezierPath(ovalIn: rect.insetBy(dx: 1.5, dy: 1.5)).fill()
            return true
        }
        return image
    }
}

// MARK: - Bridge client

enum BridgeError: LocalizedError {
    case unauthorized
    case http(Int, String)
    case badURL

    var errorDescription: String? {
        switch self {
        case .unauthorized: return "令牌不对（~/.codex-pet-bridge/token）"
        case .http(let code, let message): return message.isEmpty ? "HTTP \(code)" : message
        case .badURL: return "Bridge 地址无效"
        }
    }
}

final class BridgeClient {
    let baseURL: URL
    private(set) var token = ""
    private let tokenPath: String
    private let session: URLSession

    init(baseURL: URL, tokenPath: String) {
        self.baseURL = baseURL
        self.tokenPath = tokenPath
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 4
        config.requestCachePolicy = .reloadIgnoringLocalCacheData
        session = URLSession(configuration: config)
        reloadToken()
    }

    func reloadToken() {
        let text = (try? String(contentsOfFile: tokenPath, encoding: .utf8)) ?? ""
        token = text.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func dashboardURL() -> URL? {
        guard var components = URLComponents(url: baseURL, resolvingAgainstBaseURL: false) else { return nil }
        components.path = "/ui/"
        if !token.isEmpty { components.fragment = "token=\(token)" }
        return components.url
    }

    func send(_ path: String, method: String = "GET", json: [String: Any]? = nil,
              completion: @escaping (Result<Data, Error>) -> Void) {
        guard let url = URL(string: path, relativeTo: baseURL) else {
            completion(.failure(BridgeError.badURL))
            return
        }
        var request = URLRequest(url: url)
        request.httpMethod = method
        if !token.isEmpty {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        if let json = json {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = try? JSONSerialization.data(withJSONObject: json, options: [])
        }
        let task = session.dataTask(with: request) { data, response, error in
            let code = (response as? HTTPURLResponse)?.statusCode ?? 0
            let result: Result<Data, Error>
            if let error = error {
                result = .failure(error)
            } else if code == 401 {
                result = .failure(BridgeError.unauthorized)
            } else if code < 200 || code >= 300 {
                var message = ""
                if let data = data,
                   let object = try? JSONSerialization.jsonObject(with: data, options: []) as? [String: Any],
                   let text = object["error"] as? String {
                    message = text
                }
                result = .failure(BridgeError.http(code, message))
            } else {
                result = .success(data ?? Data())
            }
            DispatchQueue.main.async {
                completion(result)
            }
        }
        task.resume()
    }
}

// MARK: - App

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem?
    private let menu = NSMenu()
    private var client: BridgeClient?
    private var timer: Timer?
    private var status: StatusResponse?
    private var lastError: String?
    private var actionMessage: String?
    private var actionMessageAt = Date.distantPast
    private var serverOffset: TimeInterval = 0
    private var unauthorizedStreak = 0
    private let defaults = UserDefaults.standard
    private let bridgeLabel = "net.vcxzvfe.codex-pet-bridge"

    func applicationDidFinishLaunching(_ notification: Notification) {
        defaults.register(defaults: ["showText": true, "bridgeURL": "http://127.0.0.1:17366"])
        let base = URL(string: defaults.string(forKey: "bridgeURL") ?? "") ?? URL(string: "http://127.0.0.1:17366")!
        let tokenPath = (NSHomeDirectory() as NSString).appendingPathComponent(".codex-pet-bridge/token")
        client = BridgeClient(baseURL: base, tokenPath: tokenPath)

        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.image = PetIcon.image(tone: nil, offline: true)
        item.button?.imagePosition = .imageLeading
        item.button?.toolTip = "RLCD Pet"
        menu.delegate = self
        menu.autoenablesItems = false
        item.menu = menu
        statusItem = item

        rebuildMenu()
        refresh()
        let timer = Timer(timeInterval: 3, target: self, selector: #selector(tick), userInfo: nil, repeats: true)
        timer.tolerance = 0.5
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    @objc private func tick() {
        refresh()
    }

    private var now: Date {
        return Date().addingTimeInterval(serverOffset)
    }

    private func refresh() {
        guard let client = client else { return }
        client.send("/status") { [weak self] result in
            guard let self = self else { return }
            switch result {
            case .success(let data):
                do {
                    let decoded = try JSONDecoder().decode(StatusResponse.self, from: data)
                    self.status = decoded
                    self.lastError = nil
                    self.unauthorizedStreak = 0
                    if let serverMs = decoded.serverTimeMs {
                        self.serverOffset = serverMs / 1000 - Date().timeIntervalSince1970
                    }
                } catch {
                    self.lastError = "看不懂 Bridge 的回复（版本太旧？）"
                }
            case .failure(let error):
                if let bridgeError = error as? BridgeError, case .unauthorized = bridgeError {
                    self.unauthorizedStreak += 1
                    client.reloadToken()
                }
                self.status = nil
                self.lastError = error.localizedDescription
            }
            // The open menu is not rebuilt under the pointer; the next open shows fresh data.
            self.updateButton()
        }
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        rebuildMenu()
    }

    // MARK: status item

    private func updateButton() {
        guard let button = statusItem?.button else { return }
        guard let status = status else {
            button.image = PetIcon.image(tone: nil, offline: true)
            button.title = defaults.bool(forKey: "showText") ? " 离线" : ""
            button.toolTip = "RLCD Pet · \(lastError ?? "连不上 Bridge")"
            return
        }
        let (label, tone) = statusLabel(status.current?.status)
        let family = familyOf(status.current?.source)
        let hasAgent = status.current != nil
        button.image = PetIcon.image(tone: hasAgent ? tone : nil, offline: false)
        if defaults.bool(forKey: "showText") && hasAgent && tone != .idle {
            let short: String
            switch family {
            case "claude": short = "CC"
            case "codex": short = "CX"
            case "hermes": short = "HM"
            case "openclaw": short = "OC"
            default: short = ""
            }
            button.title = short.isEmpty ? " \(label)" : " \(short) \(label)"
        } else {
            button.title = ""
        }
        var tip = "RLCD Pet"
        if hasAgent {
            tip += " · \(familyName(family, fallback: status.current?.source)) \(label)"
        }
        if let device = status.device, device.seen == true {
            tip += device.online == true ? " · 板子在线" : " · 板子离线"
        }
        button.toolTip = tip
    }

    // MARK: menu

    private func rebuildMenu() {
        menu.removeAllItems()
        if let message = actionMessage, Date().timeIntervalSince(actionMessageAt) < 12 {
            menu.addItem(infoItem(message, bold: true))
            menu.addItem(NSMenuItem.separator())
        }
        if let status = status {
            addAgentSection(status)
            menu.addItem(NSMenuItem.separator())
            addBoardSection(status)
        } else {
            menu.addItem(infoItem(lastError.map { "连不上 Bridge：\($0)" } ?? "正在连接 Bridge…", bold: true))
            if unauthorizedStreak > 0 {
                menu.addItem(infoItem("令牌来自 ~/.codex-pet-bridge/token，和 Bridge 的不一致", bold: false))
            }
            menu.addItem(actionItem("重启 Bridge", #selector(restartBridge)))
        }
        menu.addItem(NSMenuItem.separator())
        menu.addItem(actionItem("打开控制台…", #selector(openDashboard), key: "d"))
        if status != nil {
            menu.addItem(actionItem("重启 Bridge", #selector(restartBridge)))
        }
        menu.addItem(NSMenuItem.separator())
        let showText = actionItem("在菜单栏显示状态文字", #selector(toggleShowText))
        showText.state = defaults.bool(forKey: "showText") ? .on : .off
        menu.addItem(showText)
        if #available(macOS 13.0, *) {
            let login = actionItem("登录时启动", #selector(toggleLoginItem))
            login.state = SMAppService.mainApp.status == .enabled ? .on : .off
            menu.addItem(login)
        }
        menu.addItem(actionItem("退出 PetBar", #selector(quit), key: "q"))
    }

    private func addAgentSection(_ status: StatusResponse) {
        let now = self.now
        if let current = status.current {
            let (label, tone) = statusLabel(current.status)
            let family = familyOf(current.source)
            let title = NSMutableAttributedString(
                string: "\(familyName(family, fallback: current.source)) · \(label)",
                attributes: [.font: NSFont.menuFont(ofSize: 0).bold()]
            )
            var detail: [String] = []
            let task = prettyTask(current.task)
            if !task.isEmpty { detail.append(task) }
            if family == "claude", let claude = status.realtime?.claude {
                if let tool = claude.tool, !tool.isEmpty { detail.append(tool) }
                if let project = claude.project, !project.isEmpty { detail.append(project) }
            }
            let ago = timeAgo(current.time, now: now)
            if !ago.isEmpty { detail.append(ago) }
            if !detail.isEmpty {
                title.append(NSAttributedString(
                    string: "\n" + detail.joined(separator: " · "),
                    attributes: [.font: NSFont.menuFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor]
                ))
            }
            let item = NSMenuItem(title: "", action: nil, keyEquivalent: "")
            item.attributedTitle = title
            item.image = PetIcon.dot(tone)
            menu.addItem(item)
        } else {
            menu.addItem(infoItem("还没有智能体活动", bold: true))
        }

        if let usage = status.usage {
            var parts: [String] = []
            if let today = usage.today, today != "--" { parts.append("\(usage.todayLabel ?? "TODAY") \(today)") }
            if let context = usage.context, context != "--" { parts.append("\(usage.contextLabel ?? "CONTEXT") \(context)") }
            if let quota = usage.quota, quota != "--" { parts.append("\(usage.quotaLabel ?? "QUOTA") \(quota)") }
            if !parts.isEmpty {
                menu.addItem(infoItem(parts.joined(separator: "  ·  "), bold: false, small: true))
            }
        }

        let agents = status.agents ?? []
        if agents.count > 1 || (agents.count == 1 && status.current == nil) {
            menu.addItem(NSMenuItem.separator())
            menu.addItem(sectionHeader("智能体"))
            for agent in agents {
                let family = agent.family ?? familyOf(agent.source)
                let (label, tone) = statusLabel(agent.status)
                var name = agent.project ?? ""
                if name.isEmpty { name = prettyTask(agent.task) }
                if name.isEmpty { name = familyName(family, fallback: agent.source) }
                let ago = timeAgo(agent.updatedAt, now: now)
                let item = NSMenuItem(title: "\(name) — \(label)\(ago.isEmpty ? "" : " · \(ago)")", action: nil, keyEquivalent: "")
                item.image = PetIcon.dot(tone)
                item.toolTip = agent.source
                menu.addItem(item)
            }
        }
    }

    private func addBoardSection(_ status: StatusResponse) {
        let device = status.device
        var line = "板子："
        if device?.seen != true {
            line += "还没连上 Bridge"
        } else if device?.online == true {
            var parts = ["在线"]
            if let battery = device?.battery { parts.append("\(battery)%\(device?.charging == true ? " 充电中" : "")") }
            if let temperature = device?.temperatureC { parts.append(String(format: "%.1f°C", temperature)) }
            if let humidity = device?.humidity { parts.append(String(format: "%.0f%%", humidity)) }
            if let rssi = device?.rssi { parts.append("\(rssi) dBm") }
            line += parts.joined(separator: " · ")
        } else {
            line += "离线（\(timeAgo(device?.lastSeenAt, now: now))）"
        }
        menu.addItem(infoItem(line, bold: true))

        let settings = status.settings
        let pageMenu = NSMenu()
        for (id, name) in boardPages {
            let item = actionItem(name, #selector(setPage(_:)))
            item.representedObject = id
            item.state = device?.page == id ? .on : .off
            pageMenu.addItem(item)
        }
        let pageItem = NSMenuItem(title: "板子页面", action: nil, keyEquivalent: "")
        pageItem.submenu = pageMenu
        menu.addItem(pageItem)

        let faceMenu = NSMenu()
        for (id, name) in clockFaces {
            let item = actionItem(name, #selector(chooseFace(_:)))
            item.representedObject = id
            item.state = settings?.clockStyle == id ? .on : .off
            faceMenu.addItem(item)
        }
        faceMenu.addItem(NSMenuItem.separator())
        let hour12 = actionItem("12 小时制", #selector(toggleHour12))
        hour12.state = settings?.hour12 == true ? .on : .off
        faceMenu.addItem(hour12)
        let seconds = actionItem("显示秒", #selector(toggleSeconds))
        seconds.state = settings?.showSeconds == false ? .off : .on
        faceMenu.addItem(seconds)
        let faceItem = NSMenuItem(title: "时钟表盘", action: nil, keyEquivalent: "")
        faceItem.submenu = faceMenu
        menu.addItem(faceItem)

        let eggMenu = NSMenu()
        eggMenu.autoenablesItems = false
        let animations = status.animations ?? []
        if animations.isEmpty {
            let empty = infoItem("还没有动画，在控制台里上传", bold: false)
            empty.isEnabled = false
            eggMenu.addItem(empty)
        }
        for animation in animations {
            var title = "▶ \(animation.id)"
            if let duration = animation.durationS {
                title += String(format: "  (%d:%02d)", Int(duration) / 60, Int(duration) % 60)
            }
            let item = actionItem(title, #selector(playEgg(_:)))
            item.representedObject = animation.id
            if settings?.defaultAnimation == animation.id {
                item.toolTip = "秘技默认播放这个"
            }
            eggMenu.addItem(item)
        }
        eggMenu.addItem(NSMenuItem.separator())
        let stop = actionItem("停止播放", #selector(stopEgg))
        stop.isEnabled = status.egg?.playing == true
        eggMenu.addItem(stop)
        let eggTitle = status.egg?.playing == true ? "彩蛋（播放中：\(status.egg?.id ?? "")）" : "彩蛋"
        let eggItem = NSMenuItem(title: eggTitle, action: nil, keyEquivalent: "")
        eggItem.submenu = eggMenu
        menu.addItem(eggItem)
    }

    private func infoItem(_ text: String, bold: Bool, small: Bool = false) -> NSMenuItem {
        let item = NSMenuItem(title: text, action: nil, keyEquivalent: "")
        let font: NSFont
        if small {
            font = NSFont.menuFont(ofSize: 11)
        } else if bold {
            font = NSFont.menuFont(ofSize: 0).bold()
        } else {
            font = NSFont.menuFont(ofSize: 0)
        }
        item.attributedTitle = NSAttributedString(string: text, attributes: [.font: font])
        return item
    }

    private func sectionHeader(_ text: String) -> NSMenuItem {
        let item = NSMenuItem(title: text, action: nil, keyEquivalent: "")
        item.attributedTitle = NSAttributedString(
            string: text,
            attributes: [.font: NSFont.menuFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor]
        )
        item.isEnabled = false
        return item
    }

    private func actionItem(_ title: String, _ action: Selector, key: String = "") -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.target = self
        return item
    }

    private func report(_ message: String) {
        actionMessage = message
        actionMessageAt = Date()
    }

    private func post(_ path: String, _ body: [String: Any], success: String) {
        client?.send(path, method: "POST", json: body) { [weak self] result in
            guard let self = self else { return }
            switch result {
            case .success:
                self.report(success)
            case .failure(let error):
                self.report("没成功：\(error.localizedDescription)")
            }
            self.refresh()
        }
    }

    // MARK: actions

    @objc private func setPage(_ sender: NSMenuItem) {
        guard let page = sender.representedObject as? String else { return }
        post("/settings", ["page": page], success: "板子将切到「\(sender.title)」")
    }

    @objc private func chooseFace(_ sender: NSMenuItem) {
        guard let style = sender.representedObject as? String else { return }
        post("/settings", ["clockStyle": style, "page": "clock"], success: "表盘换成「\(sender.title)」")
    }

    @objc private func toggleHour12() {
        let next = !(status?.settings?.hour12 ?? false)
        post("/settings", ["hour12": next], success: next ? "改成 12 小时制" : "改成 24 小时制")
    }

    @objc private func toggleSeconds() {
        let next = !(status?.settings?.showSeconds ?? true)
        post("/settings", ["showSeconds": next], success: next ? "显示秒" : "不显示秒")
    }

    @objc private func playEgg(_ sender: NSMenuItem) {
        guard let id = sender.representedObject as? String else { return }
        post("/egg/play", ["id": id], success: "板子几秒后开始播放 \(id)")
    }

    @objc private func stopEgg() {
        post("/egg/stop", [:], success: "已停止彩蛋")
    }

    @objc private func openDashboard() {
        guard let url = client?.dashboardURL() else { return }
        NSWorkspace.shared.open(url)
    }

    @objc private func restartBridge() {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        process.arguments = ["kickstart", "-k", "gui/\(getuid())/\(bridgeLabel)"]
        do {
            try process.run()
            report("正在重启 Bridge…")
        } catch {
            report("重启失败：\(error.localizedDescription)")
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            self?.refresh()
        }
    }

    @objc private func toggleShowText() {
        defaults.set(!defaults.bool(forKey: "showText"), forKey: "showText")
        updateButton()
    }

    @objc private func toggleLoginItem() {
        if #available(macOS 13.0, *) {
            let service = SMAppService.mainApp
            do {
                if service.status == .enabled {
                    try service.unregister()
                    report("已取消登录时启动")
                } else {
                    try service.register()
                    report("登录时会自动启动 PetBar")
                }
            } catch {
                report("没能修改登录项：\(error.localizedDescription)")
            }
        }
    }

    @objc private func quit() {
        NSApp.terminate(nil)
    }
}

extension NSFont {
    func bold() -> NSFont {
        return NSFontManager.shared.convert(self, toHaveTrait: .boldFontMask)
    }
}

// MARK: - Main

let application = NSApplication.shared
let appDelegate = AppDelegate()
application.delegate = appDelegate
_ = application.setActivationPolicy(.accessory)
application.run()
