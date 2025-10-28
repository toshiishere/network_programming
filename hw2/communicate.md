https://pai-kuan.notion.site/HW2-24ed3aba0aea80f2b7e4e0570c40bc34
# server to client

# client to server

# json format
- `User`：`{ id, name, email, passwordHash, createdAt, lastLoginAt }`
- `Room`：`{ id, name, hostUserId, visibility("public"|"private"), inviteList[], status("idle"|"playing"), createdAt }`
- `GameLog`（對局摘要與結果）：
    
    `{ id, matchId, roomId, users:[userId], startAt, endAt, results:[{userId, score, lines, maxCombo}] }`