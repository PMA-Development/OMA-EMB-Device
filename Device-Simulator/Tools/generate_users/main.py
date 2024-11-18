import csv
import json

users_to_create = 10
client_id_prefix = "sensor"
user_prefix = "sensor"
password_prefix = "sensor"
users = []

for i in range(users_to_create):
    users.append({
        "ClientId": f"{client_id_prefix}-{i}",
        "Username": f"{user_prefix}-{i}",
        "Password": f"{password_prefix}-{i}"
    })

with open('emqx_users_import.csv', 'w', newline='') as outcsv:
    writer = csv.DictWriter(outcsv, fieldnames = ["user_id", "password", "is_superuser"])
    writer.writeheader()
    writer.writerows({"user_id": row["Username"], "password": row["Password"], "is_superuser": False} for row in users)

appsettings_users = {
    "MqttClients": users
}

# Writing to sample.json
with open("appsettings_users.json", "w") as outfile:
    outfile.write(json.dumps(appsettings_users, indent=4))