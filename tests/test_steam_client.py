"""Regression for the game loader's inherited Steam paths."""
import os
import unittest
from pathlib import Path
from unittest.mock import patch

from server.ghost_coop.steam_client import SteamClient


def normalized(environment):
    return {key.casefold(): value for key, value in environment.items()}


class SteamClientTests(unittest.TestCase):
    def test_real_client_preloaded_and_inherited_routing_restored(self):
        inherited = dict(SteamAppId="668580", SteamGameId="668580", SteamPath="game-local",
                         SteamAppUser="GSE", SteamUser="GSE", SteamClientLaunch="1",
                         SteamEnv="1", SteamOverlayGameId="668580", unrelated="preserved")
        real = Path("real-steam")
        observed = []
        def load(path):
            observed.append((path, dict(os.environ)))
            return object()
        with patch.dict(os.environ, inherited, clear=True), \
                patch("server.ghost_coop.steam_client.running_steam_directory", return_value=real), \
                patch("server.ghost_coop.steam_client.C.CDLL", side_effect=load):
            client = SteamClient()
            self.assertEqual(observed[0][0], str(real / "steamclient64.dll"))
            self.assertEqual(normalized(observed[0][1]), normalized(dict(
                SteamAppId="480", SteamGameId="480", SteamPath=str(real), unrelated="preserved")))
            client.restore_environment()
            self.assertEqual(normalized(os.environ), normalized(inherited))

    def test_failed_client_load_restores_all_routing(self):
        inherited = {"SteamPath": "game-local", "SteamOverlayGameId": "668580"}
        with patch.dict(os.environ, inherited, clear=True), \
                patch("server.ghost_coop.steam_client.running_steam_directory", return_value=Path("real-steam")), \
                patch("server.ghost_coop.steam_client.C.CDLL", side_effect=OSError("load failed")):
            with self.assertRaises(OSError):
                SteamClient()
            self.assertEqual(normalized(os.environ), normalized(inherited))
