<?php
header("Content-Type: application/json; charset=UTF-8");

$key       = trim($_GET["key"]       ?? $_POST["key"]       ?? "");
$device_id = trim($_GET["device_id"] ?? $_POST["device_id"] ?? "");

if (empty($key)) {
    echo '{"status":"error","message":"Thiếu key"}';
    exit;
}
if (empty($device_id)) {
    echo '{"status":"error","message":"Thiếu device_id"}';
    exit;
}

$db = mysqli_connect("localhost", "lunex418_vn", "lunex418_vn", "lunex418_vn");
if (!$db) {
    echo '{"status":"error","message":"Lỗi kết nối DB"}';
    exit;
}
mysqli_set_charset($db, "utf8");

if (!preg_match('/^LUNEX-[a-zA-Z0-9]{6}-[a-zA-Z0-9]{6}-[a-zA-Z0-9]{6}-(P|B)(\d+)D$/i', $key, $pkg)) {
    echo '{"status":"error","message":"Key không đúng định dạng"}';
    exit;
}

$tier     = strtoupper($pkg[1]);
$max_days = (int)$pkg[2];
$package  = $tier === "P" ? "PRO" : "BASIC";

$key_escaped    = mysqli_real_escape_string($db, $key);
$device_escaped = mysqli_real_escape_string($db, $device_id);

// ── Transaction + FOR UPDATE lock cứng ──────────────────────────────────
mysqli_begin_transaction($db);

$row = mysqli_fetch_assoc(mysqli_query($db,
    "SELECT * FROM `buykeys` WHERE `key_value` = '$key_escaped' LIMIT 1 FOR UPDATE"
));

if (!$row) {
    mysqli_rollback($db);
    echo '{"status":"error","message":"Key không hợp lệ"}';
    exit;
}
if ($row["status"] === "cancelled") {
    mysqli_rollback($db);
    echo '{"status":"error","message":"Key đã bị huỷ"}';
    exit;
}

$saved_device  = trim($row["device_id"] ?? "");
$device_count  = (int)$row["device_count"];

if (empty($saved_device)) {
    // Lần đầu → lock device này vào key
    mysqli_query($db, "UPDATE `buykeys`
                       SET `device_id`            = '$device_escaped',
                           `device_registered_at` = NOW(),
                           `device_count`         = 1
                       WHERE `key_value` = '$key_escaped'");
    mysqli_commit($db);
    $device_count = 1; // gán thủ công để trả về đúng

} else {
    mysqli_commit($db);

    if ($saved_device !== $device_id) {
        echo json_encode([
            "status"        => "error",
            "message"       => "Key đang được sử dụng trên thiết bị khác",
            "device_locked" => true
        ], JSON_UNESCAPED_UNICODE);
        exit;
    }

    // Đúng thiết bị → tăng counter
    $device_count++;
    mysqli_query($db, "UPDATE `buykeys`
                       SET `device_count` = `device_count` + 1
                       WHERE `key_value` = '$key_escaped'");
}

// ── Ưu tiên cột days trong DB ────────────────────────────────────────────
if (!empty($row["days"]) && (int)$row["days"] > 0) {
    $max_days = (int)$row["days"];
}

// ── Kích hoạt lần đầu ────────────────────────────────────────────────────
$activated_at = trim($row["activated_at"] ?? "");
if (empty($activated_at) || $activated_at === "0000-00-00 00:00:00") {
    mysqli_query($db, "UPDATE `buykeys`
                       SET `activated_at` = NOW(),
                           `expires_at`   = DATE_ADD(NOW(), INTERVAL $max_days DAY)
                       WHERE `key_value`  = '$key_escaped'");
}

// ── Lấy trạng thái mới nhất ──────────────────────────────────────────────
$chk = mysqli_fetch_assoc(mysqli_query($db, "
    SELECT `expires_at`,
           (NOW() > `expires_at`)                          AS is_expired,
           TIMESTAMPDIFF(SECOND, NOW(), `expires_at`)      AS seconds_left,
           CEIL(TIMESTAMPDIFF(HOUR, NOW(), `expires_at`) / 24) AS days_left,
           UNIX_TIMESTAMP(NOW())                           AS server_ts,
           UNIX_TIMESTAMP(`expires_at`)                    AS expire_ts
    FROM `buykeys`
    WHERE `key_value` = '$key_escaped'
    LIMIT 1
"));

if ($chk["is_expired"]) {
    echo '{"status":"error","message":"Key đã hết hạn"}';
    exit;
}

echo json_encode([
    "status"            => "success",
    "package"           => $package,
    "max_days"          => $max_days,
    "days_left"         => (int)$chk["days_left"],
    "expires_at"        => $chk["expires_at"],
    "expire_in_seconds" => (int)$chk["seconds_left"],
    "server_ts"         => (int)$chk["server_ts"],
    "expire_ts"         => (int)$chk["expire_ts"],
    "device_check_count"=> $device_count,
], JSON_UNESCAPED_UNICODE);
