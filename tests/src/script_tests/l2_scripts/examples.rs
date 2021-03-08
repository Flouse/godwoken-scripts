use crate::testing_tool::chain::build_backend_manage;

use super::{new_block_info, DummyChainStore, SUM_PROGRAM, SUM_PROGRAM_CODE_HASH};
use gw_common::H256;
use gw_generator::{
    account_lock_manage::{always_success::AlwaysSuccess, AccountLockManage},
    backend_manage::Backend,
    dummy_state::DummyState,
    traits::StateExt,
    Generator, RollupContext,
};
use gw_types::{
    bytes::Bytes,
    core::ScriptHashType,
    packed::{RawL2Transaction, RollupConfig, Script},
    prelude::*,
};

#[test]
fn test_example_sum() {
    let mut tree = DummyState::default();
    let chain_view = DummyChainStore;
    let from_id: u32 = 2;
    let init_value: u64 = 0;
    let rollup_config = RollupConfig::default();

    let contract_id = tree
        .create_account_from_script(
            Script::new_builder()
                .code_hash(SUM_PROGRAM_CODE_HASH.pack())
                .args([0u8; 20].to_vec().pack())
                .hash_type(ScriptHashType::Type.into())
                .build(),
        )
        .expect("create account");

    // run handle message
    {
        let mut backend_manage = build_backend_manage(&rollup_config);
        // NOTICE in this test we won't need SUM validator
        backend_manage.register_backend(Backend {
            validator: SUM_PROGRAM.clone(),
            generator: SUM_PROGRAM.clone(),
            validator_script_type_hash: SUM_PROGRAM_CODE_HASH.clone().into(),
        });
        let mut account_lock_manage = AccountLockManage::default();
        account_lock_manage
            .register_lock_algorithm(H256::zero(), Box::new(AlwaysSuccess::default()));
        let rollup_context = RollupContext {
            rollup_config: Default::default(),
            rollup_script_hash: [42u8; 32].into(),
        };
        let generator = Generator::new(backend_manage, account_lock_manage, rollup_context);
        let mut sum_value = init_value;
        for (number, add_value) in &[(1u64, 7u64), (2u64, 16u64)] {
            let block_info = new_block_info(0, *number, 0);
            let raw_tx = RawL2Transaction::new_builder()
                .from_id(from_id.pack())
                .to_id(contract_id.pack())
                .args(Bytes::from(add_value.to_le_bytes().to_vec()).pack())
                .build();
            let run_result = generator
                .execute_transaction(&chain_view, &tree, &block_info, &raw_tx)
                .expect("construct");
            let return_value = {
                let mut buf = [0u8; 8];
                buf.copy_from_slice(&run_result.return_data);
                u64::from_le_bytes(buf)
            };
            sum_value += add_value;
            assert_eq!(return_value, sum_value);
            tree.apply_run_result(&run_result).expect("update state");
            println!("result {:?}", run_result);
        }
    }
}
