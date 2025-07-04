#
#    Copyright (c) 2023 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

# See https://github.com/project-chip/connectedhomeip/blob/master/docs/testing/python.md#defining-the-ci-test-arguments
# for details about the block below.
#
# === BEGIN CI TEST ARGUMENTS ===
# test-runner-runs:
#   run1:
#     app: ${CHIP_RVC_APP}
#     app-args: --discriminator 1234 --KVS kvs1 --trace-to json:${TRACE_APP}.json --app-pipe /tmp/rvcrunm_2_1_fifo
#     script-args: >
#       --PICS examples/rvc-app/rvc-common/pics/rvc-app-pics-values
#       --storage-path admin_storage.json
#       --commissioning-method on-network
#       --discriminator 1234
#       --passcode 20202021
#       --endpoint 1
#       --int-arg PIXIT.RVCRUNM.MODE_CHANGE_OK:0
#       --int-arg PIXIT.RVCRUNM.MODE_CHANGE_FAIL:2
#       --app-pipe /tmp/rvcrunm_2_1_fifo
#       --trace-to json:${TRACE_TEST_JSON}.json
#       --trace-to perfetto:${TRACE_TEST_PERFETTO}.perfetto
#     factory-reset: true
#     quiet: true
# === END CI TEST ARGUMENTS ===

import logging

import chip.clusters as Clusters
from chip.testing.matter_testing import MatterBaseTest, async_test_body, default_matter_test_main, type_matches
from mobly import asserts

# This test requires several additional command line arguments
# run with
# --int-arg PIXIT.RVCRUNM.MODE_CHANGE_OK:<mode id> --int-arg PIXIT.RVCRUNM.MODE_CHANGE_FAIL:<mode id>
# For running in CI, it is expected that OK=0 and FAIL=2


class TC_RVCRUNM_2_1(MatterBaseTest):

    def __init__(self, *args):
        super().__init__(*args)
        self.endpoint = 0
        self.mode_ok = 0
        self.mode_fail = 0
        self.is_ci = False

    async def read_mod_attribute_expect_success(self, endpoint, attribute):
        cluster = Clusters.Objects.RvcRunMode
        return await self.read_single_attribute_check_success(endpoint=endpoint, cluster=cluster, attribute=attribute)

    async def send_change_to_mode_cmd(self, newMode) -> Clusters.Objects.RvcRunMode.Commands.ChangeToModeResponse:
        ret = await self.send_single_cmd(cmd=Clusters.Objects.RvcRunMode.Commands.ChangeToMode(newMode=newMode), endpoint=self.endpoint)
        asserts.assert_true(type_matches(ret, Clusters.Objects.RvcRunMode.Commands.ChangeToModeResponse),
                            "Unexpected return type for ChangeToMode")
        return ret

    def pics_TC_RVCRUNM_2_1(self) -> list[str]:
        return ["RVCRUNM.S"]

    @async_test_body
    async def test_TC_RVCRUNM_2_1(self):

        asserts.assert_true('PIXIT.RVCRUNM.MODE_CHANGE_OK' in self.matter_test_config.global_test_params,
                            "PIXIT.RVCRUNM.MODE_CHANGE_OK must be included on the command line in "
                            "the --int-arg flag as PIXIT.RVCRUNM.MODE_CHANGE_OK:<mode id>")
        asserts.assert_true('PIXIT.RVCRUNM.MODE_CHANGE_FAIL' in self.matter_test_config.global_test_params,
                            "PIXIT.RVCRUNM.MODE_CHANGE_FAIL must be included on the command line in "
                            "the --int-arg flag as PIXIT.RVCRUNM.MODE_CHANGE_FAIL:<mode id>")

        self.endpoint = self.get_endpoint()
        self.mode_ok = self.matter_test_config.global_test_params['PIXIT.RVCRUNM.MODE_CHANGE_OK']
        self.mode_fail = self.matter_test_config.global_test_params['PIXIT.RVCRUNM.MODE_CHANGE_FAIL']
        self.is_ci = self.check_pics("PICS_SDK_CI_ONLY")

        asserts.assert_true(self.check_pics("RVCRUNM.S.A0000"), "RVCRUNM.S.A0000 must be supported")
        asserts.assert_true(self.check_pics("RVCRUNM.S.A0001"), "RVCRUNM.S.A0001 must be supported")
        asserts.assert_true(self.check_pics("RVCRUNM.S.C00.Rsp"), "RVCRUNM.S.C00.Rsp must be supported")
        asserts.assert_true(self.check_pics("RVCRUNM.S.C01.Tx"), "RVCRUNM.S.C01.Tx must be supported")

        attributes = Clusters.RvcRunMode.Attributes

        self.print_step(1, "Commissioning, already done")

        # Ensure that the device is in the correct state
        if self.is_ci:
            self.write_to_app_pipe({"Name": "Reset"})

        self.print_step(2, "Read SupportedModes attribute")
        supported_modes = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.SupportedModes)

        logging.info("SupportedModes: %s" % (supported_modes))

        asserts.assert_greater_equal(len(supported_modes), 2, "SupportedModes must have at least two entries!")

        modes = [m.mode for m in supported_modes]

        self.print_step(3, "Read CurrentMode attribute")

        old_current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

        logging.info("CurrentMode: %s" % (old_current_mode))

        # pick a value that's not on the list of supported modes
        invalid_mode = max(modes) + 1

        from enum import Enum

        class CommonCodes(Enum):
            SUCCESS = 0x00
            UNSUPPORTED_MODE = 0x01
            GENERIC_FAILURE = 0x02
            INVALID_IN_MODE = 0x03

        rvcRunCodes = [code.value for code in Clusters.RvcRunMode.Enums.StatusCode]

        self.print_step(4, "Send ChangeToMode command with NewMode set to %d" % (old_current_mode))

        ret = await self.send_change_to_mode_cmd(newMode=old_current_mode)
        asserts.assert_true(ret.status == CommonCodes.SUCCESS.value, "Changing the mode to the current mode should be a no-op")

        can_test_mode_failure = self.check_pics("RVCRUNM.S.M.CAN_TEST_MODE_FAILURE")
        can_manually_control = self.check_pics("RVCRUNM.S.M.CAN_MANUALLY_CONTROLLED")
        if can_test_mode_failure and can_manually_control:
            asserts.assert_true(self.mode_fail in modes,
                                "The MODE_CHANGE_FAIL PIXIT value (%d) is not a supported mode" % (self.mode_fail))
            self.print_step(5, "Manually put the device in a state from which it will FAIL to transition to mode %d" % (self.mode_fail))
            if self.is_ci:
                print("Change to RVC Run mode Cleaning")
                await self.send_change_to_mode_cmd(newMode=1)
            else:
                self.wait_for_user_input(
                    prompt_msg="Manually put the device in a state from which it will FAIL to transition to mode %d, and press Enter when ready." % (self.mode_fail))

            self.print_step(6, "Read CurrentMode attribute")
            old_current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

            logging.info("CurrentMode: %s" % (old_current_mode))

            self.print_step(7, "Send ChangeToMode command with NewMode set to %d" % (self.mode_fail))

            ret = await self.send_change_to_mode_cmd(newMode=self.mode_fail)
            st = ret.status
            is_mfg_code = st in range(0x80, 0xC0)
            is_err_code = (st == CommonCodes.GENERIC_FAILURE.value) or (
                st == CommonCodes.INVALID_IN_MODE.value) or (st in rvcRunCodes) or is_mfg_code
            asserts.assert_true(
                is_err_code, "Changing to mode %d must fail due to the current state of the device" % (self.mode_fail))
            st_text_len = len(ret.statusText)
            asserts.assert_true(st_text_len in range(1, 65), "StatusText length (%d) must be between 1 and 64" % (st_text_len))

            self.print_step(8, "Read CurrentMode attribute")
            current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

            logging.info("CurrentMode: %s" % (current_mode))

            asserts.assert_true(current_mode == old_current_mode, "CurrentMode changed after failed ChangeToMode command!")

        self.print_step(9, "Manually put the device in a state from which it will SUCCESSFULLY transition to mode %d" % (self.mode_ok))
        if self.is_ci:
            print("Continuing...")
        else:
            self.wait_for_user_input(
                prompt_msg="Manually put the device in a state from which it will SUCCESSFULLY transition to mode %d, and press Enter when ready." % (self.mode_ok))

        self.print_step(10, "Read CurrentMode attribute")
        old_current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

        logging.info("CurrentMode: %s" % (old_current_mode))

        self.print_step(11, "Send ChangeToMode command with NewMode set to %d" % (self.mode_ok))

        ret = await self.send_change_to_mode_cmd(newMode=self.mode_ok)
        asserts.assert_true(ret.status == CommonCodes.SUCCESS.value,
                            "Changing to mode %d must succeed due to the current state of the device" % (self.mode_ok))

        self.print_step(12, "Read CurrentMode attribute")
        current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

        logging.info("CurrentMode: %s" % (current_mode))

        asserts.assert_true(current_mode == self.mode_ok,
                            "CurrentMode doesn't match the argument of the successful ChangeToMode command!")

        self.print_step(13, "Send ChangeToMode command with NewMode set to %d" % (invalid_mode))

        ret = await self.send_change_to_mode_cmd(newMode=invalid_mode)
        asserts.assert_true(ret.status == CommonCodes.UNSUPPORTED_MODE.value,
                            "Attempt to change to invalid mode %d didn't fail as expected" % (invalid_mode))

        self.print_step(14, "Read CurrentMode attribute")
        current_mode = await self.read_mod_attribute_expect_success(endpoint=self.endpoint, attribute=attributes.CurrentMode)

        logging.info("CurrentMode: %s" % (current_mode))

        asserts.assert_true(current_mode == self.mode_ok, "CurrentMode changed after failed ChangeToMode command!")


if __name__ == "__main__":
    default_matter_test_main()
