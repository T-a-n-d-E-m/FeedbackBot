#!/bin/bash

# Works on Debian 10. May need tweaking for other distros.

INSTALL_DIR="/opt/FeedbackBot"

# This script will copy files from the repository to where they need to be and restart the feedbackbot.service.
# Also needs:
#     1) A group account: sudo addgroup --system feedbackbot
#     2) A user account: sudo adduser --system --ingroup feedbackbot --home=$INSTALL_DIR --disabled-login feedbackbot
#     3) A discord.token file in $INSTALL_DIR

if [ $(id -u) -ne 0 ]; then
	echo "Must run as root."
	exit
fi

systemctl stop feedbackbot

# Install the executable and change permissions and ownership
cp feedbackbot $INSTALL_DIR
chown feedbackbot:feedbackbot $INSTALL_DIR/feedbackbot
chmod 500 $INSTALL_DIR/feedbackbot

# discord.token - not versioned!
cp discord.token $INSTALL_DIR
chown feedbackbot:feedbackbot $INSTALL_DIR/discord.token
chmod 400 $INSTALL_DIR/discord.token

cp feedbackbot.service /etc/systemd/system
chown root:root /etc/systemd/system/feedbackbot.service
systemctl daemon-reload
systemctl enable feedbackbot
systemctl start feedbackbot
